#include "VtsModelIo3D.h"
#include "Cuda_Seis3D.cuh"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Seis3D {
namespace {
void require(bool ok, const std::string& message) {
    if(!ok) throw std::runtime_error("VTS: " + message);
}
struct Tag {
    std::string name;
    std::map<std::string,std::string> attributes;
    bool close=false, empty=false;
    std::string attr(const std::string& key, const std::string& fallback="") const {
        auto it=attributes.find(key); return it==attributes.end()?fallback:it->second;
    }
};
// A bounded XML subset: no entities/DTD, no compression or binary payloads.
// Unknown numeric arrays are skipped so Curvilinear metadata/metrics remain optional.
Tag nextTag(std::istream& in) {
    for(;;) {
        in>>std::ws;
        if(in.peek()==std::char_traits<char>::eof()) return {};
        require(in.get()=='<',"Unexpected text outside a DataArray.");
        if(in.peek()=='!') {
            require(in.get()=='!'&&in.get()=='-'&&in.get()=='-',"DTD and XML declarations are unsupported.");
            std::string tail; bool done=false;
            for(std::size_t i=0;i<1024*1024;++i) {
                const int c=in.get();require(c!=std::char_traits<char>::eof(),"Truncated comment.");
                tail.push_back(char(c)); if(tail.size()>3) tail.erase(tail.begin());
                if(tail=="-->") {done=true;break;}
            }
            require(done,"Oversized XML comment.");continue;
        }
        std::string text; char quote=0;
        for(;;) {
            const int value=in.get();require(value!=std::char_traits<char>::eof(),"Truncated XML tag.");
            const char c=char(value);
            if(!quote&&c=='>') break;
            if(c=='\''||c=='"') {if(!quote) quote=c;else if(quote==c) quote=0;}
            text.push_back(c);require(text.size()<65536,"Oversized XML tag.");
        }
        if(text.starts_with("?xml ")&&text.ends_with('?')) continue;
        Tag tag; std::size_t i=0;
        if(!text.empty()&&text[0]=='/') {tag.close=true;++i;}
        auto space=[&]{while(i<text.size()&&std::isspace(static_cast<unsigned char>(text[i]))) ++i;};
        const auto begin=i;
        while(i<text.size()&&!std::isspace(static_cast<unsigned char>(text[i]))&&text[i]!='/') ++i;
        tag.name=text.substr(begin,i-begin);require(!tag.name.empty(),"Empty XML tag.");
        for(;;) {
            space();if(i==text.size()) break;
            if(text[i]=='/') {++i;space();require(i==text.size()&&!tag.close,"Invalid empty tag.");tag.empty=true;break;}
            require(!tag.close,"Attributes on a closing tag.");
            const auto start=i;
            while(i<text.size()&&text[i]!='='&&!std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            const auto key=text.substr(start,i-start);space();
            require(!key.empty()&&i<text.size()&&text[i++]=='=',"Invalid XML attribute.");space();
            require(i<text.size()&&(text[i]=='"'||text[i]=='\''),"Unquoted XML attribute.");
            const char delimiter=text[i++];const auto valueStart=i;
            while(i<text.size()&&text[i]!=delimiter) ++i;
            require(i<text.size(),"Unterminated XML attribute.");
            const auto value=text.substr(valueStart,i-valueStart);++i;
            require(value.find('&')==std::string::npos,"XML entities are unsupported.");
            require(tag.attributes.emplace(key,value).second,"Duplicate XML attribute.");
        }
        return tag;
    }
}
struct Array {std::streamoff begin=0,end=0;int components=1;std::string type,tuples;};
std::streamoff skipArray(std::ifstream& in) {
    std::array<char,65536> buffer{};
    for(;;) {
        const auto start=in.tellg();in.read(buffer.data(),buffer.size());const auto got=in.gcount();
        const auto* found=static_cast<const char*>(std::memchr(buffer.data(),'<',std::size_t(got)));
        if(found) {const auto position=std::streamoff(start)+(found-buffer.data());in.clear();in.seekg(position);return position;}
        require(bool(in),"Truncated DataArray.");
    }
}
int integer(const std::string& text,int low,int high) {
    std::istringstream stream(text);stream.imbue(std::locale::classic());long long value;
    require(bool(stream>>value),"Invalid integer.");stream>>std::ws;
    require(stream.eof()&&value>=low&&value<=high,"Integer outside supported limits.");return int(value);
}
std::array<int,6> extent(const std::string& text) {
    std::istringstream stream(text);std::array<int,6> result{};
    for(int& v:result) require(bool(stream>>v)&&v>=0&&v<=int(maxPhysicalNodes3D),"Invalid grid extent.");
    stream>>std::ws;require(stream.eof(),"Invalid grid extent suffix.");return result;
}
void seek(std::ifstream& in,const Array& a) {in.clear();in.seekg(a.begin);require(bool(in),"Cannot seek model array.");}
double number(std::istream& in) {
    double value=0;require(bool(in>>value)&&std::isfinite(value),"Missing, invalid or nonfinite numeric value.");return value;
}
void ended(std::ifstream& in,const Array& a) {
    in>>std::ws;require(in.tellg()==a.end,"DataArray length does not match the grid.");
}
void material(float vp,float vs,float rho) {
    require(std::isfinite(vp)&&std::isfinite(vs)&&std::isfinite(rho)&&vp>0&&vp<=30000&&vs>=0&&vs<vp
        &&double(vp)*vp>(4.0/3.0)*vs*vs&&rho>0&&rho<=50000,"Invalid Vp/Vs/Density material.");
}
void extension(const std::filesystem::path& path) {
    auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    require(ext==".vts","Choose a .vts filename for VTK StructuredGrid.");
}
}

struct VtsModelReader3D::Impl {
    SimulationContext3D c;
    std::array<std::ifstream,3> streams;
    std::array<Array,3> properties;
    int nextPlane=0;
    Impl(const std::filesystem::path& path,SimulationContext3D defaults):c(defaults) {
        extension(path);
        require(std::filesystem::file_size(path)<=16ull*1024*1024*1024,"File exceeds the supported 16 GiB ASCII limit.");
        std::ifstream in(path,std::ios::binary);in.imbue(std::locale::classic());require(bool(in),"Cannot open model.");
        if(in.peek()==0xef) {char bom[3]{};in.read(bom,3);require(std::memcmp(bom,"\xef\xbb\xbf",3)==0,"Invalid UTF-8 marker.");}
        std::vector<std::string> stack;
        std::map<std::string,Array> fields,points;
        Array coords;bool haveCoords=false,haveGrid=false,havePiece=false,haveRoot=false;
        std::array<int,6> whole{};
        while(true) {
            const Tag tag=nextTag(in);if(tag.name.empty()) break;
            if(tag.close) {require(!stack.empty()&&stack.back()==tag.name,"Mismatched XML closing tag.");stack.pop_back();continue;}
            if(stack.empty()) {
                require(!haveRoot&&tag.name=="VTKFile"&&tag.attr("type")=="StructuredGrid","Expected one VTK StructuredGrid document.");
                require(tag.attr("compressor").empty(),"Compressed VTS is unsupported; save as uncompressed ASCII.");haveRoot=true;
            } else if(tag.name=="StructuredGrid") {
                require(stack.back()=="VTKFile"&&!haveGrid,"Expected one StructuredGrid.");
                whole=extent(tag.attr("WholeExtent"));haveGrid=true;
            } else if(tag.name=="Piece") {
                require(stack.back()=="StructuredGrid"&&haveGrid&&!havePiece,"Only single-piece VTS models are supported.");
                require(extent(tag.attr("Extent"))==whole,"Piece extent must cover the whole grid.");havePiece=true;
            } else if(tag.name=="DataArray") {
                require(stack.back()=="FieldData"||stack.back()=="PointData"||stack.back()=="Points"||stack.back()=="CellData","Invalid DataArray location.");
                require(tag.attr("format","ascii")=="ascii","Binary/appended VTS is unsupported; save as uncompressed ASCII.");
                Array a;a.components=integer(tag.attr("NumberOfComponents","1"),1,16);
                a.type=tag.attr("type");a.tuples=tag.attr("NumberOfTuples");a.begin=in.tellg();
                a.end=tag.empty?a.begin:skipArray(in);
                if(!tag.empty) {const auto end=nextTag(in);require(end.close&&end.name=="DataArray","Invalid array closing tag.");}
                if(stack.back()=="Points") {require(!haveCoords,"Multiple coordinate arrays.");coords=a;haveCoords=true;}
                else if(stack.back()!="CellData") {
                    auto& map=stack.back()=="FieldData"?fields:points;
                    require(map.size()<128&&map.emplace(tag.attr("Name"),a).second,"Duplicate or excessive named arrays.");
                }
                continue;
            } else if(tag.name=="FieldData") require(stack.back()=="StructuredGrid","Invalid FieldData location.");
            else if(tag.name=="PointData"||tag.name=="CellData"||tag.name=="Points") require(stack.back()=="Piece","Invalid point/cell data location.");
            else require(false,"Unsupported XML element: "+tag.name);
            require(stack.size()<12,"Excessive XML nesting.");if(!tag.empty) stack.push_back(tag.name);
        }
        require(haveRoot&&haveGrid&&havePiece&&haveCoords&&stack.empty(),"Incomplete StructuredGrid document.");
        const auto scalar=[&](const char* name,double fallback) {
            auto it=fields.find(name);if(it==fields.end()) return fallback;
            require(it->second.components==1,"Expected scalar metadata.");seek(in,it->second);
            const double result=number(in);ended(in,it->second);return result;
        };
        for(const char* unit:{"DepthPositiveDown","LengthUnitInMeters","VelocityUnitInMetersPerSecond","DensityUnitInKilogramsPerCubicMeter"})
            require(scalar(unit,1)==1,"Expected SI units and positive-down depth.");
        c.nx3D=whole[1]-whole[0]+1;c.ny3D=whole[3]-whole[2]+1;c.nz3D=whole[5]-whole[4]+1;
        require(c.nx3D>=16&&c.ny3D>=16&&c.nz3D>=16,"Unsupported physical grid dimensions.");
        require(std::uint64_t(c.nx3D)*c.ny3D<=maxPhysicalNodes3D/std::uint64_t(c.nz3D),"Physical node budget exceeded.");
        const auto nodes=std::uint64_t(c.nx3D)*c.ny3D*c.nz3D;
        const auto shape=[&](const Array& a,int components) {
            require(a.components==components&&(a.type=="Float32"||a.type=="Float64"),"Expected Float32/Float64 point arrays.");
            if(!a.tuples.empty()) require(integer(a.tuples,0,int(maxPhysicalNodes3D))==int(nodes),"Incorrect tuple count.");
        };
        shape(coords,3);
        // Geometry is checked before any live session is replaced: never flatten a curved mesh.
        seek(in,coords);std::array<double,3> first{};
        for(double& v:first) v=number(in);
        const double h=number(in);require(h>0&&h<=1000000,"Invalid positive X spacing.");
        const auto close=[&](double a,double b) {return std::abs(a-b)<=std::max(h*1e-6,std::abs(b)*2e-7);};
        require(close(first[0],0)&&close(first[1],0)&&close(first[2],0),"Grid origin must be (0,0,0); rebase coordinates before import.");
        c.spacing3D=float(h);seek(in,coords);
        for(int z=0;z<c.nz3D;++z) for(int y=0;y<c.ny3D;++y) for(int x=0;x<c.nx3D;++x)
            for(double expected:{x*h,y*h,z*h}) require(close(number(in),expected),"Curved, rotated or nonuniform grid: resample onto an equal-spacing Cartesian grid before import.");
        ended(in,coords);
        const double boundary=scalar("BoundaryMode",c.reflecting3D?2:c.freeSurface3D?1:0);
        const double pml=scalar("CPMLThickness",c.pml3D);
        require(boundary==std::floor(boundary)&&boundary>=0&&boundary<=2&&pml==std::floor(pml)&&pml>=0&&pml<=128,"Invalid boundary metadata.");
        c.freeSurface3D=boundary==1;c.reflecting3D=boundary==2;c.pml3D=int(pml);c.modelKind3D=ModelKind3D::Imported;
        auto layout=c;layout.manualTimeStep3D=false;validateContext3D(layout);
        require(estimateMemory3D(layout).totalBytes3D<=memoryLimit3D,"Model exceeds the sandbox memory budget.");
        const char* names[]={"Vp","Vs","Density"};
        for(int k=0;k<3;++k) {
            auto it=points.find(names[k]);require(it!=points.end(),std::string("Missing PointData ")+names[k]);
            properties[k]=it->second;shape(properties[k],1);
            streams[k].open(path,std::ios::binary);streams[k].imbue(std::locale::classic());seek(streams[k],properties[k]);
        }
        c.maxVp3D=0;
        for(std::uint64_t q=0;q<nodes;++q) {
            const float vp=float(number(streams[0])),vs=float(number(streams[1])),rho=float(number(streams[2]));
            material(vp,vs,rho);c.maxVp3D=std::max(c.maxVp3D,vp);
        }
        for(int k=0;k<3;++k) {ended(streams[k],properties[k]);seek(streams[k],properties[k]);}
        validateContext3D(c);
    }
    void plane(int z,float* output) {
        require(z==nextPlane&&z<c.nz3D&&output,"Read model planes in increasing Z order.");
        for(std::size_t q=0;q<std::size_t(c.nx3D)*c.ny3D;++q) {
            for(int k=0;k<3;++k) output[3*q+k]=float(number(streams[k]));
            material(output[3*q],output[3*q+1],output[3*q+2]);
        }
        if(++nextPlane==c.nz3D) for(int k=0;k<3;++k) ended(streams[k],properties[k]);
    }
};
VtsModelReader3D::VtsModelReader3D(const std::filesystem::path& p,SimulationContext3D c):impl(std::make_unique<Impl>(p,c)) {}
VtsModelReader3D::~VtsModelReader3D()=default;
const SimulationContext3D& VtsModelReader3D::context() const {return impl->c;}
void VtsModelReader3D::readPlane(int z,float* out) {impl->plane(z,out);}

void writeVtsModel3D(const std::filesystem::path& path,const SimulationContext3D& c,const std::function<void(int,float*)>& readPlane) {
    extension(path);require(!std::filesystem::exists(path),"Choose a new filename; destination already exists.");
    auto checked=c;validateContext3D(checked);
    require(bool(readPlane),"Missing physical material provider.");
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=std::filesystem::path(".part-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(temporary),"Cannot reserve temporary output.");
    const auto pending=temporary/"model.vts";
    try {
        std::ofstream out(pending,std::ios::binary);out.imbue(std::locale::classic());require(bool(out),"Cannot create model file.");
        const auto ext="0 "+std::to_string(c.nx3D-1)+" 0 "+std::to_string(c.ny3D-1)+" 0 "+std::to_string(c.nz3D-1);
        out<<std::setprecision(std::numeric_limits<float>::max_digits10)
            <<"<?xml version=\"1.0\"?>\n<VTKFile type=\"StructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
            <<"<StructuredGrid WholeExtent=\""<<ext<<"\">\n<FieldData>\n";
        for(const auto& [name,value]:std::map<std::string,int>{{"BoundaryMode",c.reflecting3D?2:c.freeSurface3D?1:0},{"CPMLThickness",c.pml3D},{"DepthPositiveDown",1},
            {"LengthUnitInMeters",1},{"VelocityUnitInMetersPerSecond",1},{"DensityUnitInKilogramsPerCubicMeter",1}})
            out<<"<DataArray type=\"Int32\" Name=\""<<name<<"\" NumberOfTuples=\"1\" format=\"ascii\">"<<value<<"</DataArray>\n";
        out<<"</FieldData>\n<Piece Extent=\""<<ext<<"\"><PointData Scalars=\"Vp\">\n";
        std::vector<float> plane(std::size_t(c.nx3D)*c.ny3D*3);
        const char* names[]={"Vp","Vs","Density"};
        for(int k=0;k<3;++k) {
            out<<"<DataArray type=\"Float32\" Name=\""<<names[k]<<"\" NumberOfComponents=\"1\" format=\"ascii\">\n";
            for(int z=0;z<c.nz3D;++z) {
                readPlane(z,plane.data());
                for(std::size_t q=0;q<plane.size();q+=3) {material(plane[q],plane[q+1],plane[q+2]);out<<plane[q+k]<<' ';}
                out<<'\n';require(bool(out),"Material write failed; check disk space.");
            }
            out<<"</DataArray>\n";
        }
        out<<"</PointData><CellData/><Points><DataArray type=\"Float64\" Name=\"Coordinates\" NumberOfComponents=\"3\" format=\"ascii\">\n"
            <<std::setprecision(17);
        for(int z=0;z<c.nz3D;++z) {
            for(int y=0;y<c.ny3D;++y) for(int x=0;x<c.nx3D;++x)
                out<<double(x)*c.spacing3D<<' '<<double(y)*c.spacing3D<<' '<<double(z)*c.spacing3D<<' ';
            out<<'\n';require(bool(out),"Coordinate write failed; check disk space.");
        }
        out<<"</DataArray></Points></Piece></StructuredGrid></VTKFile>\n";out.close();require(bool(out),"Cannot finalize model.");
        std::filesystem::rename(pending,path);std::filesystem::remove(temporary);
    } catch(...) {std::error_code ignored;std::filesystem::remove(pending,ignored);std::filesystem::remove(temporary,ignored);throw;}
}
}
