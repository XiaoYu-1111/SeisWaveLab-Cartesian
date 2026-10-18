#pragma once
#include "Common3D.h"

namespace Seis3D {

// Derive dimensions/CFL ceiling and select automatic or validated manual dt in integer microseconds.
void validateContext3D(SimulationContext3D& context3D);
// Compute all persistent solver, CPML, CUDA/GL staging and texture allocations before allocation.
MemoryBudget3D estimateMemory3D(const SimulationContext3D& context3D);
// Describe packed XZ/YZ/XY slices and a bounded volume, including CPML and excluding ghosts.
ViewLayout3D makeViewLayout3D(const SimulationContext3D& context3D);
// Allocate an isolated nonblocking stream and generate materials without full-volume host copies.
void initializeGpu3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D);
// Release partially initialized or complete resources; safe to call repeatedly.
void releaseGpu3D(GPUSimData3D& gpu3D) noexcept;
// Reset nine fields and CPML memories without reallocating materials or receiver geometry.
void resetGpu3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D);
// Replace frequency-dependent CPML coefficients transactionally without changing model or receiver buffers.
void refreshCpmlCoefficients3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D);
// Advance the leapfrog velocity-stress system by one dt, with a compact spatial source.
void runStep3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    const Source3D& source3D, std::uint64_t step3D);
// Extract active-domain slices/volume; optionally include visible CPML in the wave color maximum.
void extractViews3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    float* output3D, const ViewLayout3D& layout3D, const int slice3D[3], int component3D, int materialComponent3D,
    bool volumeEnabled3D, float& maximum3D, float materialRange3D[2], bool includeBoundary3D = false,float* velocityMaximum3D=nullptr);
// Replace receiver geometry using physical grid coordinates and strong allocation rollback.
void setReceivers3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    const Receiver3D* receivers3D, int count3D);
// Gather collocated vx/vy/vz into the owned pinned receiver buffer and wait only for this stream.
const float* sampleReceivers3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D);
// Query material properties at a physical node for the bottom status bar.
void probeMaterial3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    const Receiver3D& point3D, float values3D[3]);
// Upload one validated physical plane, accepting SI Vp/Vs/rho interleaved in XYZ order.
void uploadModelPlane3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    int plane3D, const float* values3D);
// Extend imported edge material values into CPML/ghost cells and synchronize the owned stream.
void finishModelUpload3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D);
// Copy one physical material plane to SI Vp/Vs/rho for streaming model export.
void downloadModelPlane3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    int plane3D, float* values3D);
// Download one physical scalar wavefield plane, using the same velocity collocation as the viewer.
void downloadWavefieldPlane3D(GPUSimData3D& gpu3D, const SimulationContext3D& context3D,
    int plane3D, int component3D, float* values3D, float* hostScratch3D);

} // namespace Seis3D
