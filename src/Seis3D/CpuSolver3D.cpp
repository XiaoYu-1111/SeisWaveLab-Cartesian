#include "CpuSolver3D.h"
#include "SolverCells3D.h"
#include <omp.h>
#include "Cuda_Seis3D.cuh"
#include <chrono>
#include <sstream>
namespace Seis3D {
void generateCpuMaterials3D(GPUSimData3D& g,const SimulationContext3D& c,bool extend) {
 #pragma omp parallel for num_threads(g.workers3D) schedule(static)
 for(std::int64_t i=0;i<std::int64_t(c.cells3D);++i) {
  if(extend) Cells3D::extendMaterialsKernel3D(g,c,size_t(i));else Cells3D::generateMaterialsKernel3D(g,c,size_t(i));
 }
}
namespace {
void ghost(GPUSimData3D& g,const SimulationContext3D& c,bool velocities) {
 if(c.freeSurface3D) {
  // The velocity closure samples neighboring interior values, then writes only ghosts.
  #pragma omp parallel for num_threads(g.workers3D) schedule(static)
  for(int y=4;y<c.gy3D-4;++y) for(int x=4;x<c.gx3D-4;++x) {
   if(velocities) Cells3D::velocityGhostKernel3D(g,c,x,y);else Cells3D::stressGhostKernel3D(g,c,x,y);
  }
 }
 if(c.reflecting3D) {
  const int n[3]={c.gx3D,c.gy3D,c.gz3D};
  for(int axis=0;axis<3;++axis) {
   #pragma omp parallel for num_threads(g.workers3D) schedule(static)
   for(int v=0;v<n[(axis+2)%3];++v) for(int u=0;u<n[(axis+1)%3];++u) Cells3D::reflectingGhostKernel3D(g,c,axis,velocities,u,v);
  }
 }
}
void sourceAt(GPUSimData3D& g,const SimulationContext3D& c,const Source3D& source,double time) {
 for(int a=0;a<27;++a) Cells3D::sourceKernel3D(g,c,source,time,a);
}
}
void runCpuStep3D(GPUSimData3D& g,const SimulationContext3D& c,const Source3D& source,std::uint64_t step) {
 ghost(g,c,false);
 #pragma omp parallel for num_threads(g.workers3D) schedule(static)
 for(int z=4;z<c.gz3D-4;++z) for(int y=4;y<c.gy3D-4;++y) for(int x=4;x<c.gx3D-4;++x) Cells3D::velocityKernel3D(g,c,x,y,z);
 if(source.kind3D==SourceKind3D::Force) sourceAt(g,c,source,double(step)*c.dt3D);
 ghost(g,c,true);
 #pragma omp parallel for num_threads(g.workers3D) schedule(static)
 for(int z=4;z<c.gz3D-4;++z) for(int y=4;y<c.gy3D-4;++y) for(int x=4;x<c.gx3D-4;++x) Cells3D::stressKernel3D(g,c,x,y,z);
 if(source.kind3D==SourceKind3D::Explosion) sourceAt(g,c,source,(double(step)+.5)*c.dt3D);
 ghost(g,c,false);
}
void extractCpuViews3D(GPUSimData3D& g,const SimulationContext3D& c,float* out,const ViewLayout3D& layout,const int slices[3],int component,int material,bool volume,bool boundary) {
 const auto count=volume?layout.totalSamples3D:layout.volumeOffset3D;
 std::vector<std::array<unsigned,5>> statistics(g.workers3D);
 std::vector<GPUSimData3D> local(g.workers3D,g);
 for(int worker=0;worker<g.workers3D;++worker) {statistics[worker]={0,0,0x7f7fffffu,0,0};local[worker].viewStatistics3D=statistics[worker].data();}
 #pragma omp parallel for num_threads(g.workers3D) schedule(static)
 for(std::int64_t i=0;i<std::int64_t(count);++i) Cells3D::extractKernel3D(local[omp_get_thread_num()],c,out,layout,slices[0],slices[1],slices[2],component,material,volume,boundary,size_t(i));
 for(const auto& stats:statistics) {
  for(int index:{0,1,3,4}) g.viewStatistics3D[index]=std::max(g.viewStatistics3D[index],stats[index]);
  g.viewStatistics3D[2]=std::min(g.viewStatistics3D[2],stats[2]);
 }
}
void sampleCpuReceivers3D(GPUSimData3D& g,const SimulationContext3D& c) {for(int r=0;r<g.receiverCount3D;++r) Cells3D::receiverKernel3D(g,c,r);}
std::string compareBackends3D(const SimulationContext3D& active,int workers) {
 int devices=0;if(cudaGetDeviceCount(&devices)!=cudaSuccess||devices<1) throw std::runtime_error("CUDA unavailable: comparison requires a supported CUDA device.");
 auto c=active;c.nx3D=c.ny3D=c.nz3D=32;c.pml3D=8;
 if(c.modelKind3D==ModelKind3D::Imported) c.modelKind3D=ModelKind3D::Uniform;
 validateContext3D(c);GPUSimData3D cpu,gpu;
 struct Cleanup {GPUSimData3D& a;GPUSimData3D& b;~Cleanup(){releaseGpu3D(a);releaseGpu3D(b);}} cleanup{cpu,gpu};
 initializeGpu3D(cpu,c,true);cpu.workers3D=workers;initializeGpu3D(gpu,c,false);
 Source3D source;source.x3D=source.y3D=source.z3D=16;
 double times[2]{};
 for(int n=0;n<24;++n) {
  auto start=std::chrono::steady_clock::now();runStep3D(cpu,c,source,n);
  if(n>=4) times[0]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  start=std::chrono::steady_clock::now();runStep3D(gpu,c,source,n);
  if(cudaStreamSynchronize(gpu.stream3D)!=cudaSuccess) throw std::runtime_error("CUDA benchmark failed");
  if(n>=4) times[1]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 }
 double diff=0,norm=0;std::vector<float> a(32*32),b(a.size()),scratch(a.size()*6);
 for(int z=0;z<32;++z) {
  downloadWavefieldPlane3D(cpu,c,z,0,a.data(),scratch.data());downloadWavefieldPlane3D(gpu,c,z,0,b.data(),scratch.data());
  for(size_t i=0;i<a.size();++i) {diff+=std::pow(double(a[i])-b[i],2);norm+=double(a[i])*a[i];}
 }
 std::ostringstream result;result<<"32^3, CPML 8, "<<workers<<" CPU threads | CPU "<<times[0]/20<<" ms/step | CUDA "<<times[1]/20
 <<" ms/step | CPU/CUDA "<<times[0]/std::max(times[1],1e-9)<<"x | Vz relative L2 "<<std::sqrt(diff/std::max(norm,1e-60));
 return result.str();
}

}
