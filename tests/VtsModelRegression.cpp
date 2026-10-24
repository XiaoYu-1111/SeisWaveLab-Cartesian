#include "VtsModelIo3D.h"
#include "Cuda_Seis3D.cuh"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Seis3D;
static void check(bool ok,const std::string& message) {if(!ok) throw std::runtime_error(message);}
static std::string read(const std::filesystem::path& p) {std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static void write(const std::filesystem::path& p,const std::string& text) {std::ofstream out(p,std::ios::binary);out<<text;check(bool(out),"Fixture write failed");}
static void replace(std::string& text,const std::string& a,const std::string& b) {
    const auto p=text.find(a);check(p!=std::string::npos,"Fixture marker missing: "+a);text.replace(p,a.size(),b);
}
static std::string changeValue(std::string text,const std::string& name,std::size_t index,double value) {
    const auto tag=text.find("Name=\""+name+"\"");check(tag!=std::string::npos,"Array marker missing");
    const auto begin=text.find('>',tag)+1,end=text.find("</DataArray>",begin);
    std::istringstream in(text.substr(begin,end-begin));std::ostringstream out;out<<std::setprecision(17);
    double v;std::size_t q=0;while(in>>v) out<<(q++==index?value:v)<<' ';
    check(q>index,"Fixture scalar index missing");text.replace(begin,end-begin,out.str());return text;
}
static void rejected(const std::filesystem::path& path,const std::string& text,SimulationContext3D c,const char* label) {
    write(path,text);bool failed=false;
    try {VtsModelReader3D reader(path,c);} catch(const std::exception&) {failed=true;}
    check(failed,std::string("Accepted invalid VTS: ")+label);
}
int main(int argc,char** argv) {
    try {
        if(argc==3&&std::string(argv[1])=="--inspect") {
            VtsModelReader3D reader(std::filesystem::u8path(argv[2]),{});const auto& c=reader.context();
            std::cout<<"READ VTS "<<c.nx3D<<'x'<<c.ny3D<<'x'<<c.nz3D<<" h="<<c.spacing3D<<" maxVp="<<c.maxVp3D<<'\n';return 0;
        }
        const auto output=std::filesystem::absolute(argv[0]).parent_path()/"checks/vts-model";
        std::filesystem::create_directories(output);
        SimulationContext3D c;c.nx3D=17;c.ny3D=18;c.nz3D=19;c.pml3D=8;c.spacing3D=3.25f;
        const auto provider=[&](int z,float* plane) {
            for(int y=0;y<c.ny3D;++y) for(int x=0;x<c.nx3D;++x) {
                const auto i=3*(y*c.nx3D+x);plane[i]=2800.f+10*x+3*y+2*z;
                plane[i+1]=1400.f+2*x+y+z;plane[i+2]=2100.f+x+2*y+z;
            }
        };
        std::string last;
        for(int mode=0;mode<3;++mode) {
            c.freeSurface3D=mode==1;c.reflecting3D=mode==2;validateContext3D(c);
            const auto path=output/("roundtrip-"+std::to_string(mode)+".vts");
            if(std::filesystem::exists(path)) std::filesystem::remove(path);
            writeVtsModel3D(path,c,provider);last=read(path);VtsModelReader3D model(path,{});
            check(model.context().nx3D==17&&model.context().ny3D==18&&model.context().nz3D==19,"Extent order changed");
            check(model.context().spacing3D==3.25f&&model.context().pml3D==8,"Geometry/boundary metadata lost");
            check(model.context().freeSurface3D==c.freeSurface3D&&model.context().reflecting3D==c.reflecting3D,"Boundary mode lost");
            check(model.context().maxVp3D==3047.f,"Maximum Vp scan incorrect");
            std::vector<float> expected(17*18*3),actual(expected.size());
            for(int z=0;z<19;++z) {provider(z,expected.data());model.readPlane(z,actual.data());check(actual==expected,"Nodal material/order changed");}
            bool preserved=false;try{writeVtsModel3D(path,c,provider);}catch(const std::exception&){preserved=true;}
            check(preserved&&read(path)==last,"Existing destination overwritten");
        }
        const auto bad=output/"invalid.vts";
        rejected(bad,last.substr(0,last.size()-20),c,"truncated XML");
        rejected(bad,changeValue(last,"Density",500,-1),c,"negative density");
        rejected(bad,changeValue(last,"Vp",500,1000),c,"negative bulk modulus");
        rejected(bad,changeValue(last,"Coordinates",3*(5+17*(7+18*9))+2,100),c,"interior curved node");
        rejected(bad,changeValue(last,"Coordinates",0,1),c,"nonzero origin");
        rejected(bad,changeValue(last,"LengthUnitInMeters",0,1000),c,"non-SI coordinates");
        std::string broken=last;replace(broken,"Name=\"Vp\"","Name=\"Other\"");rejected(bad,broken,c,"missing material array");
        broken=last;replace(broken,"format=\"ascii\"","format=\"binary\"");rejected(bad,broken,c,"unsupported encoding");
        broken=last;replace(broken,"</PointData>","</CellData>");rejected(bad,broken,c,"broken XML hierarchy");
        broken=last;replace(broken,"</DataArray>"," 2 </DataArray>");rejected(bad,broken,c,"extra metadata tuple");
        broken=last;replace(broken,"Name=\"Vp\"","Name=\"Vs\"");rejected(bad,broken,c,"duplicate scalar");
        broken=last;replace(broken,"<StructuredGrid ","<!DOCTYPE x><StructuredGrid ");rejected(bad,broken,c,"DTD");
        // Float64 arrays, extra metrics and arbitrary field order are valid interchange data.
        auto interchange=last;
        for(std::size_t p=0;(p=interchange.find("type=\"Float32\"",p))!=std::string::npos;p+=14) interchange.replace(p,14,"type=\"Float64\"");
        replace(interchange,"</FieldData>","<DataArray type=\"Float64\" Name=\"Parameter_spacing\" format=\"ascii\">3.25</DataArray></FieldData>");
        write(output/"float64-interchange.vts",interchange);VtsModelReader3D reader(output/"float64-interchange.vts",{});
        check(reader.context().spacing3D==3.25f,"Float64 interchange failed");
        const auto failedPath=output/"failed-write.vts";
        if(std::filesystem::exists(failedPath)) std::filesystem::remove(failedPath);
        bool failed=false;try {writeVtsModel3D(failedPath,c,[](int,float*){throw std::runtime_error("simulated provider failure");});}catch(const std::exception&){failed=true;}
        check(failed&&!std::filesystem::exists(failedPath),"Failed export published partial output");
        int devices=0;const bool cuda=cudaGetDeviceCount(&devices)==cudaSuccess&&devices>0;
        for(int backend=0;backend<(cuda?2:1);++backend) {
            GPUSimData3D source,loaded;
            struct Cleanup {GPUSimData3D& a;GPUSimData3D& b;~Cleanup(){releaseGpu3D(a);releaseGpu3D(b);}} cleanup{source,loaded};
            initializeGpu3D(source,c,backend==0);
            const auto file=output/(backend==0?"cpu-model.vts":"cuda-model.vts");if(std::filesystem::exists(file)) std::filesystem::remove(file);
            writeVtsModel3D(file,c,[&](int z,float* out){downloadModelPlane3D(source,c,z,out);});
            VtsModelReader3D document(file,c);const auto imported=document.context();initializeGpu3D(loaded,imported,backend==0);
            std::vector<float> a(std::size_t(c.nx3D)*c.ny3D*3),b(a.size());
            for(int z=0;z<c.nz3D;++z) {document.readPlane(z,a.data());uploadModelPlane3D(loaded,imported,z,a.data());}
            finishModelUpload3D(loaded,imported);
            for(int z=0;z<c.nz3D;++z) {
                downloadModelPlane3D(source,c,z,a.data());downloadModelPlane3D(loaded,imported,z,b.data());
                for(std::size_t q=0;q<a.size();++q) check(std::abs(a[q]-b[q])<=2e-6f*std::abs(a[q]),"Backend material round-trip mismatch");
            }
            std::cout<<"PASS: "<<(backend==0?"CPU":"CUDA")<<" physical model VTS round-trip\n";
        }
        std::cout<<"PASS: non-cubic XYZ/SI fields, three boundaries, Float64 interchange, invalid files and transactional output\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
