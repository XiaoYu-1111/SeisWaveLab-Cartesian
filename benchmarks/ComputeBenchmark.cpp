// Production-solver benchmarks: no synthetic numerical kernel, UI or image extraction.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#include <xmmintrin.h>
#endif
#include "Cuda_Check.cuh"
#include "CpuSolver2D.h"
#include "Cuda_Seis3D.cuh"
#include <omp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Clock=std::chrono::steady_clock;
struct Options {std::filesystem::path output;int threads=32,repeats=5,warmup=20,steps=100;int dimension=0,size=0;bool ftz=false;};
struct Trial {int dimension,size,threads,batch,repeat;std::size_t physical,padded;std::string backend;double totalMs,msStep;};
struct Error {double relative=0,maxAbsolute=0,peak=0;};
static void require(bool ok,const char* text) {if(!ok) throw std::runtime_error(text);}
static std::string json(const std::string& s) {std::ostringstream out;out<<'"';for(unsigned char c:s) {if(c=='"'||c=='\\') out<<'\\'<<c;else if(c>=32) out<<c;}out<<'"';return out.str();}
static double elapsed(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static double median(std::vector<double> values) {std::sort(values.begin(),values.end());return values[values.size()/2];}
static double quantile(std::vector<double> values,double q) {std::sort(values.begin(),values.end());double p=q*(values.size()-1);auto i=size_t(p);return values[i]+(values[std::min(i+1,values.size()-1)]-values[i])*(p-i);}
static Error compare(const std::vector<float>& cpu,const std::vector<float>& gpu) {
 require(cpu.size()==gpu.size(),"Field sizes differ");long double norm=0,difference=0;Error result;
 for(size_t i=0;i<cpu.size();++i) {require(std::isfinite(cpu[i])&&std::isfinite(gpu[i]),"Non-finite wavefield");double d=double(cpu[i])-gpu[i];norm+=double(cpu[i])*cpu[i];difference+=d*d;result.maxAbsolute=std::max(result.maxAbsolute,std::abs(d));result.peak=std::max(result.peak,double(std::abs(cpu[i])));}
 require(norm>1e-50,"Benchmark did not generate a nonzero wavefield");result.relative=std::sqrt(double(difference/norm));require(result.relative<2e-4,"CPU/CUDA relative L2 error exceeds 2e-4");return result;
}
static SimulationContext context2D(int size,int steps) {
 SimulationContext c{};c.NX=c.NZ=size;c.total_grid=int(checkedGridElementCount(size,size));c.h=10;c.dt=.0005f;c.nt=steps;c.npml=16;c.flag_type=3;c.upFlag=true;c.externalPml=true;
 c.c1_h=float(kStaggeredDerivativeWeights[0])/c.h;c.c2_h=float(kStaggeredDerivativeWeights[1])/c.h;c.c3_h=float(kStaggeredDerivativeWeights[2])/c.h;c.c4_h=float(kStaggeredDerivativeWeights[3])/c.h;
 c.rho.resize(c.total_grid);c.mu.resize(c.total_grid);c.lambda.resize(c.total_grid);c.lambda2mu.resize(c.total_grid);
 for(int z=0;z<size;++z) for(int x=0;x<size;++x) {int i=z*size+x;bool layer=z>=size/2;float vp=layer?4200:3000,vs=layer?2400:1700,rho=layer?2500:2200;c.rho[i]=rho;c.mu[i]=rho*vs*vs;c.lambda2mu[i]=rho*vp*vp;c.lambda[i]=c.lambda2mu[i]-2*c.mu[i];}
 c.dx.assign(size,0);c.dx_half.assign(size,0);c.dz.assign(size,0);c.dz_half.assign(size,0);c.dp_flat.assign(size,1);
 c.wavelet.resize(steps);for(int n=0;n<steps;++n) {double p=3.141592653589793*30*(n*double(c.dt)-.04);c.wavelet[n]=float((1-2*p*p)*std::exp(-p*p));}
 c.src_z_idx=size/4;c.src_idx=c.src_z_idx*size+size/2;c.src_type=0;c.src_amp=1;c.num_rcv=0;return c;
}
static void clear2D(GPUSimData& g) {
 for(float* p:{g.d_vx_1,g.d_vx_2,g.d_vz_1,g.d_vz_2,g.d_sxx_1,g.d_sxx_2,g.d_szz_1,g.d_szz_2,g.d_sxz_1,g.d_sxz_2}) CUDA_CHECK(solverZero2D(g,p,0,size_t(g.total_grid)*sizeof(float)));
 if(!g.cpu) CUDA_CHECK(cudaDeviceSynchronize());
}
static Seis3D::SimulationContext3D context3D(int size) {
 Seis3D::SimulationContext3D c;c.nx3D=c.ny3D=c.nz3D=size;c.pml3D=12;c.spacing3D=10;c.frequency3D=30;c.freeSurface3D=true;c.reflecting3D=false;
 c.modelKind3D=Seis3D::ModelKind3D::TwoLayer;c.manualTimeStep3D=true;c.requestedDtMs3D=.5f;Seis3D::validateContext3D(c);return c;
}
struct Suite {
 Options options;std::ofstream trials,summary;std::vector<Trial> all;
 explicit Suite(Options o):options(std::move(o)) {
  std::filesystem::create_directories(options.output);trials.open(options.output/"trials.csv");summary.open(options.output/"summary.csv");require(bool(trials)&&bool(summary),"Cannot create result files");
  trials<<"dimension,size,physical_nodes,padded_nodes,backend,threads,sync_batch,repeat,total_ms,ms_per_step\n";
  summary<<"dimension,size,physical_nodes,padded_nodes,backend,threads,sync_batch,median_ms_step,p25_ms_step,p75_ms_step,min_ms_step,max_ms_step,steps_per_second,physical_mnodes_per_second,padded_mnodes_per_second,cpu_over_cuda,relative_l2,max_abs_error,cpu_peak\n";
  trials<<std::setprecision(12);summary<<std::setprecision(12);
 }
 void append(const Trial& t) {all.push_back(t);trials<<t.dimension<<','<<t.size<<','<<t.physical<<','<<t.padded<<','<<t.backend<<','<<t.threads<<','<<t.batch<<','<<t.repeat<<','<<t.totalMs<<','<<t.msStep<<'\n';trials.flush();std::cout<<"  "<<t.backend<<" batch="<<t.batch<<" repeat="<<t.repeat<<" "<<std::fixed<<std::setprecision(4)<<t.msStep<<" ms/step"<<std::endl;}
 void summarize(int dim,int size,const Error& error) {
  double cpu=0;for(int mode=0;mode<3;++mode) {
   std::vector<double> values;Trial sample{};
   for(const auto& t:all) if(t.dimension==dim&&t.size==size&&t.backend==(mode==0?"CPU":"CUDA")&&t.batch==(mode==2?8:1)) {sample=t;values.push_back(t.msStep);}
   require(values.size()==size_t(options.repeats),"Incomplete benchmark trials");double ms=median(values);if(mode==0) cpu=ms;
   summary<<dim<<','<<size<<','<<sample.physical<<','<<sample.padded<<','<<sample.backend<<','<<sample.threads<<','<<sample.batch<<','<<ms<<','<<quantile(values,.25)<<','<<quantile(values,.75)<<','<<*std::min_element(values.begin(),values.end())<<','<<*std::max_element(values.begin(),values.end())<<','<<1000/ms<<','<<sample.physical/1000.0/ms<<','<<sample.padded/1000.0/ms<<','<<cpu/ms<<','<<error.relative<<','<<error.maxAbsolute<<','<<error.peak<<'\n';summary.flush();
  }
  std::cout<<"  validated CPU/CUDA Vz relative L2 "<<std::scientific<<error.relative<<std::endl;
 }
 void twoD(int size) {
  const auto& o=options;auto c=context2D(size,o.warmup+o.steps);GPUSimData cpu,gpu;
  struct Cleanup {GPUSimData& a;GPUSimData& b;~Cleanup(){releaseGpuSimulation(a);releaseGpuSimulation(b);}} cleanup{cpu,gpu};
  auto init=Clock::now();initializeGpuSimulation(cpu,c,true);cpu.workers=o.threads;initializeGpuSimulation(gpu,c,false);CUDA_CHECK(cudaDeviceSynchronize());
  std::cout<<"CASE 2D "<<size<<" physical="<<c.total_grid<<" padded="<<gpu.total_grid<<" dt_ms="<<c.dt*1000<<" init_s="<<elapsed(init)/1000<<std::endl;
  std::vector<float> reference,device,batched;
  for(int mode=0;mode<3;++mode) for(int repeat=0;repeat<o.repeats;++repeat) {auto& g=mode==0?cpu:gpu;int batch=mode==2?8:1;
   if(!g.cpu) {
    // Park the preceding CPU worker team and bring the GPU to a sustained compute state.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    auto heat=Clock::now();int n=0;
    do {for(int k=0;k<8;++k) runGpuSimulationStep(g,(n++)%c.nt,c,-1);CUDA_CHECK(cudaDeviceSynchronize());}while(elapsed(heat)<300);
   }
   clear2D(g);
   for(int n=0;n<o.warmup;++n) {runGpuSimulationStep(g,n,c,-1);if(!g.cpu&&(n+1)%batch==0) CUDA_CHECK(cudaDeviceSynchronize());}
   if(!g.cpu) CUDA_CHECK(cudaDeviceSynchronize());auto start=Clock::now();
   for(int n=0;n<o.steps;++n) {runGpuSimulationStep(g,o.warmup+n,c,-1);if(!g.cpu&&(n+1)%batch==0) CUDA_CHECK(cudaDeviceSynchronize());}
   if(!g.cpu) CUDA_CHECK(cudaDeviceSynchronize());double ms=elapsed(start);
   append({2,size,g.cpu?o.threads:0,batch,repeat+1,size_t(c.total_grid),size_t(g.total_grid),g.cpu?"CPU":"CUDA",ms,ms/o.steps});
   if(repeat==o.repeats-1) {auto& output=mode==0?reference:mode==1?device:batched;output.resize(c.total_grid);copyWavefieldToHost(g,output.data());}
  }
  auto error=compare(reference,device);require(compare(device,batched).relative<1e-6,"CUDA batching changed the wavefield");summarize(2,size,error);
 }
 void threeD(int size) {
  using namespace Seis3D;const auto& o=options;auto c=context3D(size);GPUSimData3D cpu,gpu;
  struct Cleanup {GPUSimData3D& a;GPUSimData3D& b;~Cleanup(){releaseGpu3D(a);releaseGpu3D(b);}} cleanup{cpu,gpu};
  auto init=Clock::now();initializeGpu3D(cpu,c,true);cpu.workers3D=o.threads;initializeGpu3D(gpu,c,false);CUDA_CHECK(cudaStreamSynchronize(gpu.stream3D));
  Source3D source;source.kind3D=SourceKind3D::Force;source.x3D=source.y3D=size/2;source.z3D=size/4;
  std::cout<<"CASE 3D "<<size<<" physical="<<size_t(size)*size*size<<" padded="<<c.cells3D<<" dt_ms="<<c.dt3D*1000<<" init_s="<<elapsed(init)/1000<<std::endl;
  std::vector<float> reference,device,batched;
  for(int mode=0;mode<3;++mode) for(int repeat=0;repeat<o.repeats;++repeat) {auto& g=mode==0?cpu:gpu;int batch=mode==2?8:1;
   if(!g.cpu3D) {
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    auto heat=Clock::now();int n=0;
    do {for(int k=0;k<8;++k) runStep3D(g,c,source,n++);CUDA_CHECK(cudaStreamSynchronize(g.stream3D));}while(elapsed(heat)<300);
   }
   resetGpu3D(g,c);if(!g.cpu3D) CUDA_CHECK(cudaStreamSynchronize(g.stream3D));
   for(int n=0;n<o.warmup;++n) {runStep3D(g,c,source,n);if(!g.cpu3D&&(n+1)%batch==0) CUDA_CHECK(cudaStreamSynchronize(g.stream3D));}
   if(!g.cpu3D) CUDA_CHECK(cudaStreamSynchronize(g.stream3D));auto start=Clock::now();
   for(int n=0;n<o.steps;++n) {runStep3D(g,c,source,o.warmup+n);if(!g.cpu3D&&(n+1)%batch==0) CUDA_CHECK(cudaStreamSynchronize(g.stream3D));}
   if(!g.cpu3D) CUDA_CHECK(cudaStreamSynchronize(g.stream3D));double ms=elapsed(start);
   append({3,size,g.cpu3D?o.threads:0,batch,repeat+1,size_t(size)*size*size,c.cells3D,g.cpu3D?"CPU":"CUDA",ms,ms/o.steps});
   if(repeat==o.repeats-1) {
    auto& output=mode==0?reference:mode==1?device:batched;output.resize(size_t(size)*size*size);std::vector<float> scratch(size_t(size)*size*6);
    for(int z=0;z<size;++z) downloadWavefieldPlane3D(g,c,z,0,output.data()+size_t(z)*size*size,scratch.data());
   }
  }
  auto error=compare(reference,device);require(compare(device,batched).relative<1e-6,"CUDA batching changed the wavefield");summarize(3,size,error);
 }
};
static void metadata(const Options& o) {
 cudaDeviceProp p{};CUDA_CHECK(cudaGetDeviceProperties(&p,0));int runtime=0,driver=0;CUDA_CHECK(cudaRuntimeGetVersion(&runtime));CUDA_CHECK(cudaDriverGetVersion(&driver));
 std::string cpuName="unknown";unsigned logical=omp_get_num_procs(),cores=0;bool ac=false;unsigned long long ram=0;bool elevatedPriority=false;unsigned long long affinity=0;
#ifdef _WIN32
 int data[4]{};std::array<char,49> name{};for(int i=0;i<3;++i) {__cpuid(data,0x80000002+i);std::memcpy(name.data()+i*16,data,16);}cpuName=name.data();
 DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);std::vector<unsigned char> records(bytes);
 if(GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(records.data()),&bytes)) for(DWORD pos=0;pos<bytes;) {auto info=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(records.data()+pos);if(info->Relationship==RelationProcessorCore) ++cores;pos+=info->Size;}
 SYSTEM_POWER_STATUS power{};if(GetSystemPowerStatus(&power)) ac=power.ACLineStatus==1;
 MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);if(GlobalMemoryStatusEx(&memory)) ram=memory.ullTotalPhys;
 DWORD_PTR processMask=0,systemMask=0;if(GetProcessAffinityMask(GetCurrentProcess(),&processMask,&systemMask)) affinity=processMask;
 elevatedPriority=SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)!=FALSE;
#endif
 int actual=0;std::vector<unsigned> originalMxcsr(o.threads),configuredMxcsr(o.threads);
 #pragma omp parallel num_threads(o.threads)
 {
 #pragma omp single
  actual=omp_get_num_threads();
#ifdef _WIN32
  auto index=omp_get_thread_num();originalMxcsr[index]=_mm_getcsr();
  auto value=o.ftz?originalMxcsr[index]|0x8040u:originalMxcsr[index]&~0x8040u;
  _mm_setcsr(value);configuredMxcsr[index]=_mm_getcsr();
#endif
 }
 require(actual==o.threads,"OpenMP did not create requested workers");
 std::ofstream out(o.output/"hardware.json");out<<std::setprecision(14)<<"{\n  \"cpu\": "<<json(cpuName)<<",\n  \"physical_cores\": "<<cores<<",\n  \"logical_processors\": "<<logical<<",\n  \"requested_threads\": "<<o.threads<<",\n  \"flush_subnormals\": "<<(o.ftz?"true":"false")<<",\n  \"original_mxcsr\": "<<originalMxcsr.front()<<",\n  \"configured_mxcsr\": "<<configuredMxcsr.front()<<",\n  \"actual_threads\": "<<actual<<",\n  \"process_affinity_mask\": "<<affinity<<",\n  \"ram_bytes\": "<<ram<<",\n  \"ac_power\": "<<(ac?"true":"false")<<",\n  \"above_normal_priority\": "<<(elevatedPriority?"true":"false")<<",\n  \"gpu\": "<<json(p.name)<<",\n  \"gpu_memory_bytes\": "<<p.totalGlobalMem<<",\n  \"gpu_sms\": "<<p.multiProcessorCount<<",\n  \"compute_capability\": "<<json(std::to_string(p.major)+"."+std::to_string(p.minor))<<",\n  \"cuda_runtime\": "<<runtime<<",\n  \"cuda_driver_api\": "<<driver<<",\n  \"msvc\": "<<_MSC_VER<<",\n  \"repeats\": "<<o.repeats<<",\n  \"warmup_steps_per_trial\": "<<o.warmup<<",\n  \"timed_steps_per_trial\": "<<o.steps<<",\n  \"dt_ms\": 0.5,\n  \"spacing_m\": 10,\n  \"frequency_hz\": 30,\n  \"precision\": \"Float32\",\n  \"2d_scheme\": \"Type3 free surface, external PML 16, two-layer\",\n  \"3d_scheme\": \"free surface, CPML 12, two-layer\",\n  \"timer\": \"steady_clock wall time, final GPU completion included\",\n  \"gpu_preconditioning_ms\": 300,\n  \"pre_gpu_cpu_park_ms\": 600,\n  \"trial_order\": \"CPU repeats, CUDA sync1 repeats, CUDA sync8 repeats\",\n  \"timed_scope\": \"stress, velocity, source, boundary, CPML, launch and synchronization; no allocation, reset, visualization, receiver recording, export or validation transfer\"\n}\n";
 std::cout<<"CPU "<<cpuName<<" cores="<<cores<<" logical="<<logical<<" workers="<<actual<<" FTZ="<<o.ftz<<" AC="<<ac<<" GPU "<<p.name<<std::endl;
}
int main(int argc,char** argv) {
 try {
  Options o;for(int i=1;i<argc;++i) {std::string arg=argv[i];require(i+1<argc,"Option value missing");std::string value=argv[++i];if(arg=="--output") o.output=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));else if(arg=="--threads") o.threads=std::stoi(value);else if(arg=="--repeats") o.repeats=std::stoi(value);else if(arg=="--steps") o.steps=std::stoi(value);else if(arg=="--warmup") o.warmup=std::stoi(value);else if(arg=="--dimension") o.dimension=std::stoi(value);else if(arg=="--size") o.size=std::stoi(value);else if(arg=="--ftz") o.ftz=std::stoi(value)!=0;else throw std::invalid_argument("Unknown option");}
  require(!o.output.empty()&&o.threads>0&&o.threads<=omp_get_num_procs()&&o.steps>0&&o.warmup>=0&&o.repeats>=3&&o.repeats%2==1,"Invalid output, thread or repeat configuration");
  omp_set_dynamic(0);omp_set_num_threads(o.threads);Suite suite(o);metadata(o);
  if(o.dimension==0||o.dimension==2) for(int size:o.size?std::vector<int>{o.size}:std::vector<int>{256,1024,2048}) suite.twoD(size);
  if(o.dimension==0||o.dimension==3) for(int size:o.size?std::vector<int>{o.size}:std::vector<int>{48,96,160}) suite.threeD(size);
  std::ofstream success(o.output/"COMPLETE.txt");success<<"All timings and CPU/CUDA wavefield checks completed.\n";std::cout<<"COMPLETE "<<o.output<<std::endl;return 0;
 } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<std::endl;return 1;}
}
