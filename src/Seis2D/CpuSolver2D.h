#pragma once
#include "Cuda_Check.cuh"
#include <cstdlib>
#include <cstring>
// Pointer ownership follows GPUSimData::cpu for the entire lifetime of a session.
template<class T> inline cudaError_t solverAllocate2D(const GPUSimData& g,T** p,size_t n) {
 if(!g.cpu) return cudaMalloc(p,n);
 *p=static_cast<T*>(std::malloc(n));return *p||!n?cudaSuccess:cudaErrorMemoryAllocation;
}
inline cudaError_t solverFree2D(const GPUSimData& g,void* p) {if(g.cpu) {std::free(p);return cudaSuccess;}return p?cudaFree(p):cudaSuccess;}
inline cudaError_t solverCopy2D(const GPUSimData& g,void* dst,const void* src,size_t n,cudaMemcpyKind kind) {
 if(g.cpu) {std::memcpy(dst,src,n);return cudaSuccess;}return cudaMemcpy(dst,src,n,kind);
}
inline cudaError_t solverZero2D(const GPUSimData& g,void* dst,int value,size_t n) {
 if(g.cpu) {std::memset(dst,value,n);return cudaSuccess;}return cudaMemset(dst,value,n);
}
void runCpuStep2D(GPUSimData& g,int iteration,const SimulationContext& ctx,int sourceCount);
void recordCpuStep2D(GPUSimData& g,int count,float* vx,float* vz);
float generateCpuDisplay2D(GPUSimData& g,float4* output,float gain,int component,bool automatic);
int cpuWorkerLimit2D();
