#include "VtiModelIO.h"
#include "VtkXmlModel.h"
#include <bit>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <limits>
#include <vector>

namespace SeisCommon {
namespace {
using namespace VtkXml;
struct VtiArray: Array {bool appended=false;std::uint64_t offset=0;};
std::uint64_t unsignedValue(const std::string& text) {
    std::uint64_t v=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),v);
    require(error==std::errc{}&&end==text.data()+text.size(),"Invalid array offset.");return v;
}
int width(const std::string& type) {
    if(type=="Float32"||type=="Int32"||type=="UInt32") return 4;
    if(type=="Float64"||type=="Int64"||type=="UInt64") return 8;
    if(type=="Int16"||type=="UInt16") return 2;
    if(type=="Int8"||type=="UInt8") return 1;
    require(false,"Unsupported scalar type.");return 0;
}
std::uint64_t bits(std::istream& in,int bytes,bool little) {
    unsigned char b[8]{};in.read(reinterpret_cast<char*>(b),bytes);require(bool(in),"Truncated binary array.");
    std::uint64_t v=0;for(int i=0;i<bytes;++i) v|=std::uint64_t(b[i])<<(8*(little?i:bytes-1-i));return v;
}
double value(std::ifstream& in,const VtiArray& a,bool little) {
    if(!a.appended) return number(in);
    require(in.tellg()>=a.begin&&in.tellg()+std::streamoff(width(a.type))<=a.end,"Array shorter than expected.");
    const auto v=bits(in,width(a.type),little);double result=0;
    if(a.type=="Float32") result=std::bit_cast<float>(std::uint32_t(v));
    else if(a.type=="Float64") result=std::bit_cast<double>(v);
    else if(a.type=="Int32") result=std::bit_cast<std::int32_t>(std::uint32_t(v));
    else if(a.type=="Int64") result=double(std::bit_cast<std::int64_t>(v));
    else if(a.type=="Int16") result=std::bit_cast<std::int16_t>(std::uint16_t(v));
    else if(a.type=="Int8") result=std::bit_cast<std::int8_t>(std::uint8_t(v));
    else result=double(v);
    require(std::isfinite(result),"Nonfinite binary value.");return result;
}
void endArray(std::ifstream& in,const VtiArray& a) {
    if(a.appended) require(in.tellg()==a.end,"Binary array size does not match the grid.");else ended(in,a);
}
// Decode complete chunks instead of querying a file position and issuing a read per scalar.
void readFloats(std::ifstream& in,const VtiArray& a,bool little,std::size_t count,float* out) {
    if(!a.appended) {for(std::size_t q=0;q<count;++q) out[q]=float(number(in));return;}
    const int bytes=width(a.type);std::vector<unsigned char> data(count*bytes);
    in.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()));require(bool(in),"Truncated material array.");
    for(std::size_t q=0;q<count;++q) {
        std::uint64_t v=0;for(int k=0;k<bytes;++k) v|=std::uint64_t(data[q*bytes+k])<<(8*(little?k:bytes-1-k));
        out[q]=bytes==4?std::bit_cast<float>(std::uint32_t(v)):float(std::bit_cast<double>(v));
    }
}
void checkExtension(const std::filesystem::path& p) {
    auto ext=p.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    require(ext==".vti","Choose a .vti ImageData model filename.");
}
std::size_t nodes(const ModelGrid& g,std::size_t limit) {
    require(g.nx>0&&g.ny>0&&g.nz>0,"Invalid dimensions.");
    require(std::uint64_t(g.nx)*g.ny<=limit/std::uint64_t(g.nz),"Physical node budget exceeded.");
    for(int k=0;k<3;++k) require(std::isfinite(g.origin[k])&&std::isfinite(g.spacing[k])&&g.spacing[k]>0,"Invalid origin or spacing.");
    return std::size_t(g.nx)*g.ny*g.nz;
}
std::array<double,3> triple(const std::string& text) {
    std::istringstream in(text);in.imbue(std::locale::classic());std::array<double,3> result{};
    for(double& v:result) v=number(in);in>>std::ws;require(in.eof(),"Expected three geometry values.");return result;
}
void writeBits(std::ostream& out,std::uint64_t v,int bytes) {
    unsigned char b[8]{};for(int k=0;k<bytes;++k) b[k]=static_cast<unsigned char>(v>>(8*k));out.write(reinterpret_cast<const char*>(b),bytes);
}
}

struct VtiModelReader::Impl {
    ModelGrid g;
    std::array<std::ifstream,3> streams;
    std::array<VtiArray,3> arrays;
    bool little=true;
    float maxVp=0;
    int nextPlane=0;
    std::function<void(float)> progress;
    Impl(const std::filesystem::path& path,std::size_t limit,std::function<void(float)> notify):progress(std::move(notify)) {
        if(progress) progress(0);
        checkExtension(path);const auto fileBytes=std::filesystem::file_size(path);
        require(fileBytes<=16ull*1024*1024*1024,"File exceeds the 16 GiB model limit.");
        std::ifstream in(path,std::ios::binary);in.imbue(std::locale::classic());require(bool(in),"Cannot open model.");
        if(in.peek()==0xef) {char bom[3]{};in.read(bom,3);require(std::memcmp(bom,"\xef\xbb\xbf",3)==0,"Invalid UTF-8 marker.");}
        std::vector<std::string> stack;std::map<std::string,VtiArray> fields,points;
        std::vector<VtiArray> all;
        std::array<int,6> whole{};int headerBytes=4;
        bool root=false,image=false,piece=false,raw=false;std::streamoff rawStart=0;
        auto resolve=[&](VtiArray a) {
            if(!a.appended) return a;
            require(raw&&std::uint64_t(rawStart)<=fileBytes&&a.offset<=fileBytes-std::uint64_t(rawStart),"Invalid appended offset.");
            const auto start=std::uint64_t(rawStart)+a.offset;
            require(fileBytes-start>=std::uint64_t(headerBytes),"Truncated array header.");in.clear();in.seekg(std::streamoff(start));
            const auto bytes=bits(in,headerBytes,little);
            require(bytes<=fileBytes-start-headerBytes&&bytes%width(a.type)==0,"Invalid binary array byte count.");
            a.begin=std::streamoff(start+headerBytes);a.end=a.begin+std::streamoff(bytes);return a;
        };
        for(;;) {
            const auto tag=nextTag(in);if(tag.name.empty()) break;
            if(tag.close) {require(!stack.empty()&&stack.back()==tag.name,"Mismatched XML closing tag.");stack.pop_back();continue;}
            if(stack.empty()) {
                require(!root&&tag.name=="VTKFile"&&tag.attr("type")=="ImageData","Expected one VTK ImageData document.");root=true;
                require(tag.attr("compressor").empty(),"Compressed VTI is unsupported; use uncompressed raw-appended or ASCII data.");
                const auto order=tag.attr("byte_order","LittleEndian");require(order=="LittleEndian"||order=="BigEndian","Invalid byte order.");little=order=="LittleEndian";
                const auto header=tag.attr("header_type","UInt32");require(header=="UInt32"||header=="UInt64","Invalid header type.");headerBytes=header=="UInt64"?8:4;
            } else if(tag.name=="ImageData") {
                require(stack.back()=="VTKFile"&&!image,"Expected one ImageData grid.");image=true;
                whole=extent(tag.attr("WholeExtent"));require(whole[0]==0&&whole[2]==0&&whole[4]==0,"Model extent must begin at zero.");
                g.nx=whole[1]+1;g.ny=whole[3]+1;g.nz=whole[5]+1;
                g.origin=triple(tag.attr("Origin","0 0 0"));g.spacing=triple(tag.attr("Spacing","1 1 1"));nodes(g,limit);
                const auto direction=tag.attr("Direction");
                if(!direction.empty()) {
                    std::istringstream d(direction);d.imbue(std::locale::classic());
                    for(int k=0;k<9;++k) require(number(d)==(k%4==0?1:0),"Rotated ImageData is unsupported.");d>>std::ws;require(d.eof(),"Invalid direction matrix.");
                }
            } else if(tag.name=="Piece") {
                require(stack.back()=="ImageData"&&!piece&&extent(tag.attr("Extent"))==whole,"Expected a single Piece covering the complete grid.");piece=true;
            } else if(tag.name=="DataArray") {
                require(stack.back()=="FieldData"||stack.back()=="PointData"||stack.back()=="CellData","Invalid DataArray location.");
                VtiArray a;a.type=tag.attr("type");width(a.type);a.components=integer(tag.attr("NumberOfComponents","1"),1,16);a.tuples=tag.attr("NumberOfTuples");
                const auto format=tag.attr("format","ascii");require(format=="ascii"||format=="appended","Use ASCII or raw-appended VTI arrays.");
                a.appended=format=="appended";a.begin=in.tellg();
                if(a.appended) {a.offset=unsignedValue(tag.attr("offset"));require(tag.empty,"Appended array tags must be empty.");}
                else {a.end=tag.empty?a.begin:skipArray(in);if(!tag.empty) {const auto close=nextTag(in);require(close.close&&close.name=="DataArray","Invalid array closing tag.");}}
                require(all.size()<256,"Excessive arrays.");all.push_back(a);
                if(stack.back()!="CellData") {
                    auto& map=stack.back()=="FieldData"?fields:points;
                    require(map.emplace(tag.attr("Name"),a).second,"Duplicate array name.");
                }
                continue;
            } else if(tag.name=="AppendedData") {
                require(stack.back()=="VTKFile"&&image&&piece&&!raw&&!tag.empty&&tag.attr("encoding")=="raw","Expected one raw AppendedData section.");
                in>>std::ws;require(in.get()=='_',"Missing appended-data marker.");rawStart=in.tellg();raw=true;
                auto end=rawStart;
                for(const auto& a:all) if(a.appended) end=std::max(end,resolve(a).end);
                in.clear();in.seekg(end);const auto close=nextTag(in);require(close.close&&close.name=="AppendedData","Truncated or excess appended payload.");continue;
            } else if(tag.name=="FieldData") require(stack.back()=="ImageData","Invalid FieldData location.");
            else if(tag.name=="PointData"||tag.name=="CellData") require(stack.back()=="Piece","Invalid point/cell data location.");
            else require(false,"Unsupported XML element: "+tag.name);
            require(stack.size()<12,"Excessive XML nesting.");if(!tag.empty) stack.push_back(tag.name);
        }
        require(root&&image&&piece&&stack.empty(),"Incomplete ImageData document.");
        for(auto& [name,a]:fields) a=resolve(a);
        for(auto& [name,a]:points) a=resolve(a);
        auto scalar=[&](const char* name,double fallback) {
            auto it=fields.find(name);if(it==fields.end()) return fallback;const auto& a=it->second;
            require(a.components==1,"Expected scalar metadata.");seek(in,a);const double v=value(in,a,little);endArray(in,a);return v;
        };
        for(const char* unit:{"DepthPositiveDown","LengthUnitInMeters","VelocityUnitInMetersPerSecond","DensityUnitInKilogramsPerCubicMeter"})
            require(scalar(unit,1)==1,"Expected SI units and positive-down depth.");
        for(const char* key:{"BoundaryMode","CPMLThickness","Dimension","SolverScheme"}) if(fields.contains(key)) {
            const double v=scalar(key,0);require(v==std::floor(v)&&v>=0&&v<=10000,"Invalid integer model metadata.");g.metadata[key]=int(v);
        }
        const auto n=nodes(g,limit);const char* names[]={"Vp","Vs","Density"};
        for(int k=0;k<3;++k) {
            const auto it=points.find(names[k]);require(it!=points.end(),std::string("Missing PointData ")+names[k]);arrays[k]=it->second;const auto& a=arrays[k];
            require(a.components==1&&(a.type=="Float32"||a.type=="Float64"),"Expected Float32/Float64 scalar materials.");
            if(!a.tuples.empty()) require(unsignedValue(a.tuples)==n,"Incorrect tuple count.");
            if(a.appended) require(std::uint64_t(a.end-a.begin)==n*width(a.type),"Material array byte count does not match dimensions.");
            streams[k].open(path,std::ios::binary);streams[k].imbue(std::locale::classic());seek(streams[k],a);
        }
        if(progress) progress(.05f);
        std::array<std::vector<float>,3> chunk;
        for(auto& channel:chunk) channel.resize((std::min)(n,std::size_t(65536)));
        for(std::size_t begin=0;begin<n;begin+=65536) {
            const auto count=(std::min)(n-begin,std::size_t(65536));
            for(int k=0;k<3;++k) readFloats(streams[k],arrays[k],little,count,chunk[k].data());
            for(std::size_t q=0;q<count;++q) {material(chunk[0][q],chunk[1][q],chunk[2][q]);maxVp=std::max(maxVp,chunk[0][q]);}
            if(progress) progress(.05f+.75f*float(double(begin+count)/n));
        }
        for(int k=0;k<3;++k) {endArray(streams[k],arrays[k]);seek(streams[k],arrays[k]);}
    }
    void plane(int z,float* out) {
        require(out&&z==nextPlane&&z<g.nz,"Read planes in increasing Z order.");
        const auto count=std::size_t(g.nx)*g.ny;std::vector<float> channel(count);
        for(int k=0;k<3;++k) {readFloats(streams[k],arrays[k],little,count,channel.data());for(std::size_t q=0;q<count;++q) out[3*q+k]=channel[q];}
        for(std::size_t q=0;q<count;++q) material(out[3*q],out[3*q+1],out[3*q+2]);
        if(++nextPlane==g.nz) for(int k=0;k<3;++k) endArray(streams[k],arrays[k]);
        if(progress) progress(.8f+.2f*float(nextPlane)/g.nz);
    }
};
VtiModelReader::VtiModelReader(const std::filesystem::path& p,std::size_t limit,std::function<void(float)> progress):impl(std::make_unique<Impl>(p,limit,std::move(progress))) {}
VtiModelReader::~VtiModelReader()=default;
const ModelGrid& VtiModelReader::grid() const {return impl->g;}
float VtiModelReader::maximumVp() const {return impl->maxVp;}
void VtiModelReader::readPlane(int z,float* out) {impl->plane(z,out);}

void writeVtiModel(const std::filesystem::path& path,const ModelGrid& g,const std::function<void(int,float*)>& provider) {
    checkExtension(path);const auto n=nodes(g,36000000);require(bool(provider),"Missing material provider.");
    require(!std::filesystem::exists(path),"Choose a new filename; destination already exists.");
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=std::filesystem::path(".part-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(temporary),"Cannot reserve temporary output.");const auto pending=temporary/"model.vti";
    try {
        std::ofstream out(pending,std::ios::binary);out.imbue(std::locale::classic());require(bool(out),"Cannot create model file.");
        const auto ext="0 "+std::to_string(g.nx-1)+" 0 "+std::to_string(g.ny-1)+" 0 "+std::to_string(g.nz-1);
        out<<std::setprecision(17)<<"<?xml version=\"1.0\"?>\n<VTKFile type=\"ImageData\" version=\"1.0\" byte_order=\"LittleEndian\" header_type=\"UInt64\">\n"
            <<"<ImageData WholeExtent=\""<<ext<<"\" Origin=\""<<g.origin[0]<<' '<<g.origin[1]<<' '<<g.origin[2]
            <<"\" Spacing=\""<<g.spacing[0]<<' '<<g.spacing[1]<<' '<<g.spacing[2]<<"\">\n<FieldData>\n";
        auto metadata=g.metadata;
        for(const char* key:{"DepthPositiveDown","LengthUnitInMeters","VelocityUnitInMetersPerSecond","DensityUnitInKilogramsPerCubicMeter"}) metadata[key]=1;
        for(const auto& [name,v]:metadata) {
            require(!name.empty()&&name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"Invalid metadata name.");
            out<<"<DataArray type=\"Int32\" Name=\""<<name<<"\" NumberOfTuples=\"1\" format=\"ascii\">"<<v<<"</DataArray>\n";
        }
        const auto bytes=std::uint64_t(n)*4;const auto block=bytes+8;const char* names[]={"Vp","Vs","Density"};
        out<<"</FieldData><Piece Extent=\""<<ext<<"\"><PointData Scalars=\"Vp\">\n";
        for(int k=0;k<3;++k) out<<"<DataArray type=\"Float32\" Name=\""<<names[k]<<"\" NumberOfComponents=\"1\" format=\"appended\" offset=\""<<k*block<<"\"/>\n";
        out<<"</PointData><CellData/></Piece></ImageData>\n<AppendedData encoding=\"raw\">\n_";
        std::vector<float> plane(std::size_t(g.nx)*g.ny*3),channel(plane.size()/3);
        for(int k=0;k<3;++k) {
            writeBits(out,bytes,8);
            for(int z=0;z<g.nz;++z) {
                provider(z,plane.data());for(std::size_t q=0;q<channel.size();++q) {material(plane[3*q],plane[3*q+1],plane[3*q+2]);channel[q]=plane[3*q+k];}
                if constexpr(std::endian::native==std::endian::little) out.write(reinterpret_cast<const char*>(channel.data()),std::streamsize(channel.size()*4));
                else for(float v:channel) writeBits(out,std::bit_cast<std::uint32_t>(v),4);
                require(bool(out),"Write failed; check disk space.");
            }
        }
        out<<"\n</AppendedData>\n</VTKFile>\n";out.close();require(bool(out),"Cannot finalize model.");
        require(!std::filesystem::exists(path),"Destination now exists; choose a new filename.");std::filesystem::rename(pending,path);std::filesystem::remove(temporary);
    } catch(...) {std::error_code ignored;std::filesystem::remove(pending,ignored);std::filesystem::remove(temporary,ignored);throw;}
}
}
