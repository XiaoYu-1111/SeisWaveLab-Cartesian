#include "VtiModelIO.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace SeisCommon;
static void check(bool ok,const std::string& message) {if(!ok) throw std::runtime_error(message);}
static std::string read(const std::filesystem::path& p) {std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static void write(const std::filesystem::path& p,const std::string& s) {std::ofstream out(p,std::ios::binary);out.write(s.data(),s.size());check(bool(out),"Fixture write failed");}
static void replace(std::string& s,const std::string& a,const std::string& b) {auto p=s.find(a);check(p!=std::string::npos,"Fixture marker missing");s.replace(p,a.size(),b);}
static void rejected(const std::filesystem::path& p,const std::string& s,const char* why) {
    write(p,s);bool failed=false;try {VtiModelReader reader(p);}catch(const std::exception&){failed=true;}
    check(failed,std::string("Invalid file accepted: ")+why);
}
static std::string changedScalar(std::string s,int component,std::size_t index,float v) {
    const auto marker=s.find("\n_",s.find("<AppendedData"))+2;
    const auto offset=marker+std::size_t(component)*(8+17*18*19*4)+8+index*4;
    const auto bits=std::bit_cast<std::uint32_t>(v);
    for(int k=0;k<4;++k) s[offset+k]=char(bits>>(8*k));return s;
}
int main(int argc,char** argv) {
 try {
    if(argc==3&&std::string(argv[1])=="--inspect") {
        VtiModelReader reader(std::filesystem::u8path(argv[2]));const auto& g=reader.grid();
        std::cout<<"READ VTI "<<g.nx<<'x'<<g.ny<<'x'<<g.nz<<" h="<<g.spacing[0]<<" maxVp="<<reader.maximumVp()<<'\n';return 0;
    }
    const auto out=std::filesystem::absolute(argv[0]).parent_path()/"checks/vti-model";std::filesystem::create_directories(out);
    std::string text;
    for(int dimension:{2,3}) {
        ModelGrid g;g.nx=17;g.ny=dimension==2?1:18;g.nz=19;g.spacing={3.25,3.25,3.25};
        g.metadata={{"Dimension",dimension},{"BoundaryMode",1},{"CPMLThickness",12}};
        auto provider=[&](int z,float* p) {for(int y=0;y<g.ny;++y) for(int x=0;x<g.nx;++x) {
            const auto q=3*(y*g.nx+x);p[q]=2800+10.f*x+3.f*y+2.f*z;p[q+1]=1400+2.f*x+y+z;p[q+2]=2100+x+2.f*y+z;
        }};
        const auto path=out/(dimension==2?"model-2d.vti":"model-3d.vti");if(std::filesystem::exists(path)) std::filesystem::remove(path);
        writeVtiModel(path,g,provider);VtiModelReader model(path);const auto& a=model.grid();
        check(a.nx==g.nx&&a.ny==g.ny&&a.nz==g.nz&&a.spacing==g.spacing,"Geometry changed");
        check(a.metadata.at("Dimension")==dimension&&a.metadata.at("CPMLThickness")==12,"Metadata lost");
        std::vector<float> expected(g.nx*g.ny*3),actual(expected.size());
        for(int z=0;z<g.nz;++z) {provider(z,expected.data());model.readPlane(z,actual.data());check(expected==actual,"Nodal order or values changed");}
        check(std::filesystem::file_size(path)<std::size_t(g.nx)*g.ny*g.nz*12+4096,"Unexpected point-coordinate or text overhead");
        text=read(path);bool preserved=false;try{writeVtiModel(path,g,provider);}catch(const std::exception&){preserved=true;}
        check(preserved&&read(path)==text,"Existing destination overwritten");
        const auto fail=out/"failed.vti";if(std::filesystem::exists(fail)) std::filesystem::remove(fail);
        bool failed=false;try{writeVtiModel(fail,g,[](int,float*){throw std::runtime_error("simulated failure");});}catch(...){failed=true;}
        check(failed&&!std::filesystem::exists(fail),"Partial output published");
        std::cout<<"PASS: "<<dimension<<"D physical model, XYZ order, bit-exact Float32, SI metadata and transactional output\n";
    }
    const auto bad=out/"invalid.vti";
    rejected(bad,text.substr(0,text.size()/2),"truncated payload");
    rejected(bad,changedScalar(text,2,500,-1),"negative density");
    rejected(bad,changedScalar(text,0,500,1000),"negative bulk modulus");
    rejected(bad,changedScalar(text,0,500,std::numeric_limits<float>::infinity()),"nonfinite material");
    auto changed=text;replace(changed,"Name=\"Vp\"","Name=\"Other\"");rejected(bad,changed,"missing Vp");
    changed=text;replace(changed,"Name=\"Vp\"","Name=\"Vs\"");rejected(bad,changed,"duplicate array");
    changed=text;replace(changed,"offset=\"0\"","offset=\"99999999999999999\"");rejected(bad,changed,"invalid appended offset");
    changed=text;replace(changed,"byte_order=\"LittleEndian\"","byte_order=\"Other\"");rejected(bad,changed,"byte order");
    changed=text;replace(changed,"<ImageData ","<!DOCTYPE x><ImageData ");rejected(bad,changed,"DTD");
    changed=text;replace(changed,"<VTKFile ","<VTKFile compressor=\"vtkZLibDataCompressor\" ");rejected(bad,changed,"unsupported compression");
    changed=text;replace(changed,"Spacing=\"3.25 3.25 3.25\"","Spacing=\"0 3.25 3.25\"");rejected(bad,changed,"zero spacing");
    changed=text;replace(changed,"type=\"ImageData\"","type=\"StructuredGrid\"");rejected(bad,changed,"wrong dataset type");
    changed=text;replace(changed,"Name=\"LengthUnitInMeters\" NumberOfTuples=\"1\" format=\"ascii\">1", "Name=\"LengthUnitInMeters\" NumberOfTuples=\"1\" format=\"ascii\">0");rejected(bad,changed,"non-SI units");
    // Independent minimal binary fixtures cover both byte orders and both VTK block header sizes.
    for(bool little:{true,false}) for(int header:{4,8}) for(int scalar:{4,8}) {
        std::ostringstream xml;std::string payload;
        auto append=[&](std::uint64_t value,int bytes) {for(int k=0;k<bytes;++k) payload.push_back(char(value>>(8*(little?k:bytes-1-k))));};
        xml<<"<VTKFile type=\"ImageData\" byte_order=\""<<(little?"LittleEndian":"BigEndian")<<"\" header_type=\"UInt"<<header*8<<"\"><ImageData WholeExtent=\"0 1 0 0 0 2\" Spacing=\"2 2 2\"><Piece Extent=\"0 1 0 0 0 2\"><PointData>";
        const char* names[]={"Vp","Vs","Density"};const double base[]={3000,1600,2200};
        for(int k=0;k<3;++k) {
            xml<<"<DataArray Name=\""<<names[k]<<"\" type=\"Float"<<scalar*8<<"\" format=\"appended\" offset=\""<<payload.size()<<"\"/>";
            append(6*scalar,header);
            for(int q=0;q<6;++q) append(scalar==4?std::uint64_t(std::bit_cast<std::uint32_t>(float(base[k]+q))):std::bit_cast<std::uint64_t>(base[k]+q),scalar);
        }
        const auto fixture=out/"interchange.vti";
        write(fixture,xml.str()+"</PointData></Piece></ImageData><AppendedData encoding=\"raw\">_"+payload+"</AppendedData></VTKFile>");
        VtiModelReader model(fixture);check(model.maximumVp()==3005,"Independent binary fixture maximum changed");
        float plane[6]{};
        for(int z=0;z<3;++z) {model.readPlane(z,plane);for(int x=0;x<2;++x) for(int k=0;k<3;++k) check(plane[3*x+k]==base[k]+2*z+x,"Independent binary fixture changed values/order");}
    }
    std::cout<<"PASS: eight independent endian, UInt32/64 header and Float32/64 payload combinations\n";
    // Independent ASCII Float64 fixture; no dependence on the binary writer's representation.
    std::ostringstream ascii;ascii<<"<?xml version=\"1.0\"?><VTKFile type=\"ImageData\" byte_order=\"LittleEndian\"><ImageData WholeExtent=\"0 16 0 0 0 18\" Origin=\"0 0 0\" Spacing=\"2.5 2.5 2.5\"><Piece Extent=\"0 16 0 0 0 18\"><PointData>";
    for(const char* name:{"Density","Vp","Vs"}) {
        ascii<<"<DataArray type=\"Float64\" Name=\""<<name<<"\" format=\"ascii\">";
        for(int q=0;q<17*19;++q) ascii<<(std::string(name)=="Vp"?3000:std::string(name)=="Vs"?1700:2200)<<' ';
        ascii<<"</DataArray>";
    }
    ascii<<"</PointData></Piece></ImageData></VTKFile>";write(out/"ascii64.vti",ascii.str());VtiModelReader plain(out/"ascii64.vti");
    check(plain.maximumVp()==3000&&plain.grid().spacing[0]==2.5,"ASCII Float64 import failed");
    float progress=-1;int updates=0;
    VtiModelReader monitored(out/"model-3d.vti",36000000,[&](float value){check(value>=progress&&value<=1,"Import progress went backwards/outside range");progress=value;++updates;});
    std::vector<float> monitoredPlane(17*18*3);
    for(int z=0;z<19;++z) monitored.readPlane(z,monitoredPlane.data());
    check(progress==1&&updates>19,"Import progress did not cover validation and reading");
    bool cancelled=false;
    try {VtiModelReader stop(out/"model-3d.vti",36000000,[](float value){if(value>.1f) throw std::runtime_error("cancel");});}catch(const std::exception&){cancelled=true;}
    check(cancelled,"Progress cancellation did not interrupt validation");
    std::cout<<"PASS: monotone validation/read progress and cancellation callbacks\n";
    std::cout<<"PASS: ASCII Float64 interchange, invalid XML/data/offsets/units and compact binary size\n";return 0;
 }catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
