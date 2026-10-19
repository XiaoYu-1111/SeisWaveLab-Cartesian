#include "Cuda_Seis3D.cuh"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace Seis3D;
static void require(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
int main() {
 try {
  int count=0;bool available=cudaGetDeviceCount(&count)==cudaSuccess&&count>0;
  for(int mode=0;mode<3;++mode) {
   SimulationContext3D c;c.nx3D=c.ny3D=c.nz3D=24;c.pml3D=8;c.freeSurface3D=mode==1;c.reflecting3D=mode==2;
   validateContext3D(c);GPUSimData3D cpu,serial,gpu;
   struct Cleanup {GPUSimData3D& a;GPUSimData3D& b;GPUSimData3D& c;~Cleanup(){releaseGpu3D(a);releaseGpu3D(b);releaseGpu3D(c);}} cleanup{cpu,serial,gpu};
   initializeGpu3D(cpu,c,true);cpu.workers3D=4;initializeGpu3D(serial,c,true);serial.workers3D=1;
   if(available) initializeGpu3D(gpu,c);
   Receiver3D receivers[2]={{12,12,8},{14,13,12}};
   setReceivers3D(cpu,c,receivers,2);setReceivers3D(serial,c,receivers,2);if(available) setReceivers3D(gpu,c,receivers,2);
   Source3D source;source.x3D=source.y3D=12;source.z3D=8;
   source.kind3D=mode==1?SourceKind3D::Explosion:SourceKind3D::Force;
   for(int n=0;n<64;++n) {runStep3D(cpu,c,source,n);runStep3D(serial,c,source,n);if(available) runStep3D(gpu,c,source,n);}
   std::vector<float> a(24*24),b(a.size()),s(a.size()),scratch(a.size()*6);
   double diff=0,norm=0;
   for(int z=0;z<24;++z) {
    downloadWavefieldPlane3D(cpu,c,z,0,a.data(),scratch.data());downloadWavefieldPlane3D(serial,c,z,0,s.data(),scratch.data());
    require(a==s,"3D CPU parallel differs from serial");
    if(available) downloadWavefieldPlane3D(gpu,c,z,0,b.data(),scratch.data());
    for(size_t k=0;k<a.size();++k) {require(std::isfinite(a[k]),"3D CPU non-finite field");norm+=double(a[k])*a[k];if(available) diff+=std::pow(double(a[k])-b[k],2);}
   }
   require(norm>0,"3D CPU source not injected");if(available) require(std::sqrt(diff/norm)<2e-4,"3D CPU/CUDA Vz mismatch");
   const float* samples=sampleReceivers3D(cpu,c);for(int i=0;i<6;++i) require(std::isfinite(samples[i]),"CPU receiver non-finite");
   if(available) {const auto* other=sampleReceivers3D(gpu,c);for(int i=0;i<6;++i) require(std::abs(samples[i]-other[i])<std::sqrt(norm)*2e-4,"3D CPU/CUDA receiver mismatch");}
   auto layout=makeViewLayout3D(c);std::vector<float> view(layout.totalSamples3D*2);int slices[]={12,12,8};float peak=0,range[2]{},vm=0;
   extractViews3D(cpu,c,view.data(),layout,slices,0,5,false,peak,range,false,&vm);require(peak>0&&range[0]>0&&vm>0,"CPU slice extraction failed");
   for(int n=0;n<24;++n) {std::vector<float> material(24*24*3);downloadModelPlane3D(cpu,c,n,material.data());uploadModelPlane3D(serial,c,n,material.data());}
   finishModelUpload3D(serial,c);float material[3]{};probeMaterial3D(serial,c,receivers[0],material);require(std::abs(material[0]-c.vp3D)<.1f,"CPU material plane roundtrip failed");
   refreshCpmlCoefficients3D(cpu,c);resetGpu3D(cpu,c);downloadWavefieldPlane3D(cpu,c,8,0,a.data(),scratch.data());for(float value:a) require(value==0,"CPU reset failed");
   setReceivers3D(cpu,c,nullptr,0);require(cpu.receiverCount3D==0,"CPU receiver cleanup failed");
   std::cout<<"PASS 3D mode "<<mode<<": serial/OpenMP, "<<(available?"CUDA parity, ":"")<<"receivers, views, model roundtrip, reset\n";
  }
  return 0;
 }catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
