#include "CpuSolver2D.h"
#include "SolverCells2D.h"
#include <algorithm>
#include <omp.h>
int cpuWorkerLimit2D() {return omp_get_num_procs();}
namespace {
void sweep2D(GPUSimData& g,void (*cell)(GPUSimData,int,int)) {
 const int height=g.NZ,width=g.NX,workers=g.workers;
 #pragma omp parallel for num_threads(workers) schedule(static)
 for(int row=0;row<height;++row) for(int col=0;col<width;++col) cell(g,row,col);
}
}
void runCpuStep2D(GPUSimData& g,int iteration,const SimulationContext& ctx,int sourceCount) {
 using namespace Seis2DCells;
 if(g.flag_type==2) sweep2D(g,updateType2Stress);
 else if(g.flag_type==3&&g.upFlag) sweep2D(g,updateType3Stress);
 else sweep2D(g,updateType1Stress);
 if(g.flag_type==3&&g.upFlag) for(int j=0;j<g.NX;++j) mirrorType3Stress(g,j);
 if(g.flag_type==2) sweep2D(g,updateType2Velocity);
 else if(g.flag_type==3&&g.upFlag) sweep2D(g,updateType3Velocity);
 else sweep2D(g,updateType1Velocity);
 // Serial injection preserves summation order when several sources share a cell.
 if(sourceCount>=0) for(int n=0;n<sourceCount;++n) {
  auto s=g.d_active_sources[n];float v=3.14159265358979323846f*s.f_peak*(s.t-1.0f/s.f_peak);
  float value=(1-2*v*v)*std::exp(-v*v)*s.amp*kSourceInjectionScale2D;
  applyPointSource(g,value,s.idx);
 } else if(iteration<ctx.nt&&iteration<int(ctx.wavelet.size())) {
  int z=ctx.src_z_idx+(g.physicalNX>0?g.originZ:0);
  if(g.flag_type==3&&g.upFlag) z=std::max(z,g.fs_idx+1);
  int x=g.physicalNX>0?ctx.src_idx%g.physicalNX+g.originX:ctx.src_idx%g.NX;
  applyPointSource(g,ctx.wavelet[iteration]*ctx.src_amp*kSourceInjectionScale2D,z*g.NX+x);
 }
 // Corners share writes, so this inexpensive pass stays serial.
 for(int k=0;k<std::max(g.NX,g.NZ);++k) zeroBoundary(g,k);
}
void recordCpuStep2D(GPUSimData& g,int count,float* vx,float* vz) {
 for(int r=0;r<count;++r) {int k=g.d_rcv_grid_idx[r];
  vx[r]=g.flag_type==2?g.d_vx[k]:g.d_vx_1[k]+g.d_vx_2[k];
  vz[r]=g.flag_type==2?g.d_vz[k]:g.d_vz_1[k]+g.d_vz_2[k];
 }
}
float generateCpuDisplay2D(GPUSimData& g,float4* output,float gain,int component,bool automatic) {
 float peak=0;std::vector<float> peaks(g.workers,0);
 #pragma omp parallel for num_threads(g.workers) schedule(static)
 for(int i=0;i<g.NZ;++i) for(int j=0;j<g.NX;++j) {
  float balance=1,value=Seis2DCells::component(g,i,j,component,balance);
  output[i*g.NX+j]=make_float4(value,0,balance,1);
  bool physical=g.physicalNX<=0||(j>=g.originX&&j<g.originX+g.physicalNX&&i>=g.originZ&&i<g.originZ+g.physicalNZ);
  if(physical&&std::isfinite(value)) {auto& local=peaks[omp_get_thread_num()];local=std::max(local,std::abs(value*balance));}
 }
 for(float value:peaks) peak=std::max(peak,value);
 float reference=automatic?std::max(peak,1e-30f)/std::max(gain/20.0f,1e-6f):1/std::max(gain,1e-6f);
 #pragma omp parallel for num_threads(g.workers) schedule(static)
 for(int k=0;k<g.total_grid;++k) output[k].y=gain>0?std::clamp(output[k].x*output[k].z/reference,-1.0f,1.0f):0;
 return reference;
}
