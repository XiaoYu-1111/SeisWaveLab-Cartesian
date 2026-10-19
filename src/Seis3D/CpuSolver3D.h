#pragma once
#include "Common3D.h"
#include <cstdlib>
#include <string>
#include <cstring>
namespace Seis3D {
template<class T> inline cudaError_t allocateBackend3D(const GPUSimData3D& g,T** p,size_t n) {if(!g.cpu3D) return cudaMalloc(p,n);*p=static_cast<T*>(std::malloc(n));return *p||!n?cudaSuccess:cudaErrorMemoryAllocation;}
template<class T> inline cudaError_t allocateHostBackend3D(const GPUSimData3D& g,T** p,size_t n) {if(!g.cpu3D) return cudaMallocHost(p,n);return allocateBackend3D(g,p,n);}
inline cudaError_t freeBackend3D(const GPUSimData3D& g,void* p) {if(g.cpu3D) {std::free(p);return cudaSuccess;}return p?cudaFree(p):cudaSuccess;}
inline cudaError_t freeHostBackend3D(const GPUSimData3D& g,void* p) {if(g.cpu3D) return freeBackend3D(g,p);return p?cudaFreeHost(p):cudaSuccess;}
inline cudaError_t syncBackend3D(const GPUSimData3D& g,cudaStream_t stream) {return g.cpu3D?cudaSuccess:cudaStreamSynchronize(stream);}
inline cudaError_t zeroBackend3D(const GPUSimData3D& g,void* p,int value,size_t n,cudaStream_t stream) {if(g.cpu3D) {std::memset(p,value,n);return cudaSuccess;}return cudaMemsetAsync(p,value,n,stream);}
inline cudaError_t copyBackend3D(const GPUSimData3D& g,void* dst,const void* src,size_t n,cudaMemcpyKind kind,cudaStream_t stream) {if(g.cpu3D) {std::memcpy(dst,src,n);return cudaSuccess;}return cudaMemcpyAsync(dst,src,n,kind,stream);}
inline cudaError_t copyPlaneBackend3D(const GPUSimData3D& g,void* dst,size_t dp,const void* src,size_t sp,size_t width,size_t height,cudaMemcpyKind kind,cudaStream_t stream) {
 if(!g.cpu3D) return cudaMemcpy2DAsync(dst,dp,src,sp,width,height,kind,stream);
 for(size_t y=0;y<height;++y) std::memcpy(static_cast<char*>(dst)+y*dp,static_cast<const char*>(src)+y*sp,width);return cudaSuccess;
}
std::string compareBackends3D(const SimulationContext3D& c,int workers);
void generateCpuMaterials3D(GPUSimData3D& g,const SimulationContext3D& c,bool extend=false);
void runCpuStep3D(GPUSimData3D& g,const SimulationContext3D& c,const Source3D& source,std::uint64_t step);
void extractCpuViews3D(GPUSimData3D& g,const SimulationContext3D& c,float* out,const ViewLayout3D& layout,const int slices[3],int component,int material,bool volume,bool boundary);
void sampleCpuReceivers3D(GPUSimData3D& g,const SimulationContext3D& c);
}
