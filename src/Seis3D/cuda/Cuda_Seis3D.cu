#include "Cuda_Seis3D.cuh"
#include "SolverCells3D.h"
#include "CpuSolver3D.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace Seis3D {
namespace {
using namespace Cells3D;

// Translate CUDA failures into sandbox-local exceptions, with the failing operation named.
void checkCuda3D(cudaError_t result, const char* operation) {
    if (result != cudaSuccess)
        throw std::runtime_error(std::string("Seis3D / ") + operation + ": " + cudaGetErrorString(result));
}

// Check the next asynchronous kernel launch without synchronizing the entire device.
void checkLaunch3D() { checkCuda3D(cudaGetLastError(), "kernel launch"); }

// Allocate and clear one stream-owned float buffer; caller owns rollback on failure.
void allocateFloats3D(const GPUSimData3D& g,float*& pointer, std::size_t count, cudaStream_t stream) {
    if (count == 0) return;
    checkCuda3D(allocateBackend3D(g,&pointer, count * sizeof(float)), "allocate field");
    checkCuda3D(zeroBackend3D(g,pointer, 0, count * sizeof(float), stream), "clear field");
}

// Reject material combinations with non-positive bulk modulus or invalid SI units.
bool validMaterial3D(float vp, float vs, float rho) {
    return std::isfinite(vp) && std::isfinite(vs) && std::isfinite(rho)
        && vp > 0 && vp <= 30000 && vs >= 0 && vs < vp
        && vp * vp > (4.0f / 3.0f) * vs * vs && rho > 0 && rho <= 50000;
}

// Compute the number of slab cells for one direction, including cross-axis corner cells.
std::size_t slabCount3D(const SimulationContext3D& c, int axis) {
    if (c.reflecting3D) return 0;
    if (axis == 0) return std::size_t(2 * c.pml3D) * c.gy3D * c.gz3D;
    if (axis == 1) return std::size_t(2 * c.pml3D) * c.gx3D * c.gz3D;
    return std::size_t((c.freeSurface3D ? 1 : 2) * c.pml3D) * c.gx3D * c.gy3D;
}

// Generate standard unsplit CPML a/b/kappa coefficients at center and half-grid positions.
std::vector<float> makeCoefficients3D(const SimulationContext3D& c, int axis) {
    const int n = axis == 0 ? c.gx3D : axis == 1 ? c.gy3D : c.gz3D;
    std::vector<float> values(std::size_t(n) * 6);
    const double sigmaMax = -4.0 * c.maxVp3D * std::log(1.0e-6) / (2.0 * c.pml3D * c.spacing3D);
    for (int half = 0; half < 2; ++half) for (int i = 0; i < n; ++i) {
        const double coordinate = i + 0.5 * half;
        double depth = (coordinate - (n - stencilRadius3D - c.pml3D - 0.5)) / c.pml3D;
        if (!(axis == 2 && c.freeSurface3D))
            depth = (std::max)(depth, (stencilRadius3D + c.pml3D - 0.5 - coordinate) / c.pml3D);
        depth = (std::clamp)(depth, 0.0, 1.0);
        const double power = depth * depth * depth;
        const double sigma = sigmaMax * power, kappa = 1.0 + 2.0 * power;
        const double alpha = depth > 0 ? 3.141592653589793 * c.frequency3D * (1.0 - depth) : 0.0;
        const double b = std::exp(-(sigma / kappa + alpha) * c.dt3D);
        const double a = sigma > 0 ? sigma * (b - 1.0) / (kappa * (sigma + kappa * alpha)) : 0.0;
        const std::size_t offset = (std::size_t(half) * n + i) * 3;
        values[offset] = float(a); values[offset + 1] = float(b); values[offset + 2] = float(1.0 / kappa);
    }
    return values;
}

// Flatten an X-contiguous padded node on the device.


// Return eighth-order staggered first-derivative weights, shared by all three axes.


// Evaluate D+ at i+1/2 or D- at i, using eight samples along the selected axis.


// Apply one derivative's CPML recurrence in its direction's compact slab allocation.


// Populate a procedural model directly on the GPU, extending material properties into padding.
__global__ void generateMaterialsKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::generateMaterialsKernel3D(g,c,std::size_t(blockIdx.x)*blockDim.x+threadIdx.x);}

// Mirror one pair of cell-face slip walls with stagger-aware parity. Axis launches are ordered
// on the same stream, so edges/corners inherit the product of the independent face reflections.
// Normal velocity is odd, tangential velocity even; normal stress is even, shear traction odd.
__global__ void reflectingGhostKernel3D(GPUSimData3D g, SimulationContext3D c, int axis, bool velocities) {Cells3D::reflectingGhostKernel3D(g,c,axis,velocities,int(blockIdx.x*blockDim.x+threadIdx.x),int(blockIdx.y*blockDim.y+threadIdx.y));}

// Complete six reflecting faces before the next interior update; no damping or CPML memories.
void applyReflectingWalls3D(GPUSimData3D& g,const SimulationContext3D& c,bool velocities) {
    const int lengths[3]={c.gx3D,c.gy3D,c.gz3D};
    const dim3 block(16,16);
    for(int axis=0;axis<3;++axis) {
        const dim3 grid((lengths[(axis+1)%3]+15)/16,(lengths[(axis+2)%3]+15)/16);
        reflectingGhostKernel3D<<<grid,block,0,g.stream3D>>>(g,c,axis,velocities); checkLaunch3D();
    }
}

// Extend an imported physical domain into CPML and ghost cells without changing physical cells.
__global__ void extendMaterialsKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::extendMaterialsKernel3D(g,c,std::size_t(blockIdx.x)*blockDim.x+threadIdx.x);}

// Fill traction ghost images: szz is odd at integer z=0; sxz/syz are odd at half-grid z.
__global__ void stressGhostKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::stressGhostKernel3D(g,c,int(blockIdx.x*blockDim.x+threadIdx.x)+4,int(blockIdx.y*blockDim.y+threadIdx.y)+4);}

// Update half-time velocities at their respective face-centered density locations.
__global__ void velocityKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::velocityKernel3D(g,c,int(blockIdx.x*blockDim.x+threadIdx.x)+4,int(blockIdx.y*blockDim.y+threadIdx.y)+4,int(blockIdx.z*blockDim.z+threadIdx.z)+4);}

// Map velocity ghosts using the three zero-traction differential constraints at z=0.
// Interior differences remain eighth order; this planar surface closure is second order.
__global__ void velocityGhostKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::velocityGhostKernel3D(g,c,int(blockIdx.x*blockDim.x+threadIdx.x)+4,int(blockIdx.y*blockDim.y+threadIdx.y)+4);}

// Harmonic four-cell shear averaging preserves zero shear modulus on fluid interfaces.


// Update the six stresses using independently filtered derivatives and an unsplit constitutive law.
__global__ void stressKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::stressKernel3D(g,c,int(blockIdx.x*blockDim.x+threadIdx.x)+4,int(blockIdx.y*blockDim.y+threadIdx.y)+4,int(blockIdx.z*blockDim.z+threadIdx.z)+4);}

// Inject a normalized 3x3x3 Ricker source at the correct velocity/stress leapfrog time.
__global__ void sourceKernel3D(GPUSimData3D g, SimulationContext3D c, Source3D source, double time) {Cells3D::sourceKernel3D(g,c,source,time,int(threadIdx.x));}

// Collocate face velocities at the cell center and return the selected physical scalar.


// Resample only the bounded display volume at cell centers; solver fields and slice data stay intact.


// Extract a packed slice or the auxiliary volume, retaining a physical-unit common color maximum.
__global__ void extractKernel3D(GPUSimData3D g, SimulationContext3D c, float* output,
    ViewLayout3D layout, int sx, int sy, int sz, int component, int materialComponent, bool volume, bool includeBoundary) {Cells3D::extractKernel3D(g,c,output,layout,sx,sy,sz,component,materialComponent,volume,includeBoundary,std::size_t(blockIdx.x)*blockDim.x+threadIdx.x);}

// Gather receiver velocities into a compact interleaved buffer, validating coordinates on upload.
__global__ void receiverKernel3D(GPUSimData3D g, SimulationContext3D c) {Cells3D::receiverKernel3D(g,c,int(blockIdx.x*blockDim.x+threadIdx.x));}

} // namespace

// Validate dimensions, SI material values and the 3-D staggered CFL bound before any allocation.
void validateContext3D(SimulationContext3D& c) {
    if (c.reflecting3D) c.freeSurface3D=false;
    if (int(c.modelKind3D)<0 || int(c.modelKind3D)>int(ModelKind3D::SaltDome)
        || !std::isfinite(c.interfaceDepth3D) || c.interfaceDepth3D<.1f || c.interfaceDepth3D>.9f
        || !std::isfinite(c.layerDip3D) || std::abs(c.layerDip3D)>.6f
        || !std::isfinite(c.layerThickness3D) || c.layerThickness3D<.03f || c.layerThickness3D>.5f
        || !std::isfinite(c.faultThrow3D) || std::abs(c.faultThrow3D)>.4f
        || !std::isfinite(c.anomalyRadius3D) || c.anomalyRadius3D<.05f || c.anomalyRadius3D>.4f)
        throw std::invalid_argument("Seis3D: invalid procedural model geometry.");
    if (c.nx3D < 16 || c.ny3D < 16 || c.nz3D < 16)
        throw std::invalid_argument("Seis3D: every physical dimension must be at least 16.");
    // Divide before multiplying so malformed imported dimensions cannot overflow the node count.
    if (std::size_t(c.nx3D)>maxPhysicalNodes3D/std::size_t(c.ny3D)
        || std::size_t(c.nx3D)*c.ny3D>maxPhysicalNodes3D/std::size_t(c.nz3D))
        throw std::invalid_argument("Seis3D: physical node count exceeds 300 x 300 x 400 (36,000,000).");
    if (c.pml3D < 8 || c.pml3D > 40 || !std::isfinite(c.spacing3D) || c.spacing3D <= 0
        || !std::isfinite(c.frequency3D) || c.frequency3D <= 0 || c.frequency3D > 10000
        || !std::isfinite(c.cfl3D) || c.cfl3D <= 0 || c.cfl3D > 0.9f)
        throw std::invalid_argument("Seis3D: invalid CPML, spacing, frequency or CFL safety factor.");
    if (!validMaterial3D(c.vp3D,c.vs3D,c.density3D) || !validMaterial3D(c.layerVp3D,c.layerVs3D,c.layerDensity3D))
        throw std::invalid_argument("Seis3D: Vp/Vs/rho must be finite with positive bulk modulus.");
    if (c.modelKind3D != ModelKind3D::Imported)
        c.maxVp3D = c.modelKind3D == ModelKind3D::Uniform ? c.vp3D : (std::max)(c.vp3D,c.layerVp3D);
    if (!std::isfinite(c.maxVp3D) || c.maxVp3D <= 0 || c.maxVp3D > 30000)
        throw std::invalid_argument("Seis3D: invalid model maximum Vp.");
    const int pml=activePml3D(c);
    c.ox3D = c.oy3D = pml+4; c.oz3D = c.freeSurface3D ? 4 : pml+4;
    c.gx3D = c.nx3D+2*pml+8; c.gy3D = c.ny3D+2*pml+8;
    c.gz3D = c.nz3D+(c.freeSurface3D ? 1 : 2)*pml+8;
    c.cells3D = std::size_t(c.gx3D)*c.gy3D*c.gz3D;
    constexpr double sumWeights = 1225.0/1024 + 245.0/3072 + 49.0/5120 + 5.0/7168;
    const double safeDt = c.cfl3D*c.spacing3D/(std::sqrt(3.0)*c.maxVp3D*sumWeights);
    // An integer-us solver dt guarantees exact fixed-sampling SEG-Y rev1 headers.
    const auto microseconds = std::floor(safeDt*1.0e6);
    if (microseconds < 1 || microseconds > 65535)
        throw std::invalid_argument("Seis3D: safe dt is outside the implemented 1..65535 microsecond range.");
    c.safeDt3D = float(microseconds*1.0e-6);
    c.dt3D = c.safeDt3D;
    if (c.manualTimeStep3D) {
        const double requestedUs = double(c.requestedDtMs3D)*1000;
        const double roundedUs = std::round(requestedUs);
        if (!std::isfinite(requestedUs) || roundedUs < 1 || roundedUs > 65535)
            throw std::invalid_argument("Seis3D: manual dt must be between 0.001 and 65.535 ms.");
        // Allow float input roundoff, but reject fractional-us intervals that SEG-Y cannot represent.
        if (std::abs(requestedUs-roundedUs) > 0.05)
            throw std::invalid_argument("Seis3D: manual dt must use whole microseconds (0.001 ms increments).");
        if (roundedUs > microseconds)
            throw std::invalid_argument("Seis3D: manual dt exceeds the model CFL ceiling; reduce dt or select Automatic CFL.");
        c.dt3D = float(roundedUs*1.0e-6);
    }
}

// Cache the full active domain, including CPML but excluding ghosts, so visibility needs no allocation.
ViewLayout3D makeViewLayout3D(const SimulationContext3D& c) {
    ViewLayout3D l;
    const int pml=activePml3D(c);
    const int nx=c.nx3D+2*pml,ny=c.ny3D+2*pml,nz=c.nz3D+(c.freeSurface3D?1:2)*pml;
    l.domainSize3D[0]=nx;l.domainSize3D[1]=ny;l.domainSize3D[2]=nz;
    l.domainOrigin3D[0]=-pml;l.domainOrigin3D[1]=-pml;l.domainOrigin3D[2]=c.freeSurface3D?0:-pml;
    l.sliceWidth3D[0]=nx; l.sliceHeight3D[0]=nz;
    l.sliceWidth3D[1]=ny; l.sliceHeight3D[1]=nz;
    l.sliceWidth3D[2]=nx; l.sliceHeight3D[2]=ny;
    l.sliceOffset3D[1]=std::size_t(nx)*nz;
    l.sliceOffset3D[2]=l.sliceOffset3D[1]+std::size_t(ny)*nz;
    l.volumeOffset3D=l.sliceOffset3D[2]+std::size_t(nx)*ny;
    l.volumeX3D=(std::min)(maxVolumeAxis3D,nx); l.volumeY3D=(std::min)(maxVolumeAxis3D,ny); l.volumeZ3D=(std::min)(maxVolumeAxis3D,nz);
    l.totalSamples3D=l.volumeOffset3D+std::size_t(l.volumeX3D)*l.volumeY3D*l.volumeZ3D;
    return l;
}

// Include the nine wavefields, three materials, slab CPML, GL scalar/RGBA textures and staging.
MemoryBudget3D estimateMemory3D(const SimulationContext3D& context) {
    auto c=context; validateContext3D(c);
    MemoryBudget3D b;
    b.fieldBytes3D=c.cells3D*12*sizeof(float);
    for (int a=0;a<3;++a) b.cpmlBytes3D+=slabCount3D(c,a)*6*sizeof(float);
    const auto l=makeViewLayout3D(c);
    b.viewBytes3D=l.totalSamples3D*4*sizeof(float)+l.volumeOffset3D*4
        +std::size_t(maxVolumeOutput3D)*maxVolumeOutput3D*4;
    b.totalBytes3D=b.fieldBytes3D+b.cpmlBytes3D+b.viewBytes3D
        +std::size_t(c.gx3D+c.gy3D+c.gz3D)*6*sizeof(float)
        +maxReceivers3D*(sizeof(Receiver3D)+3*sizeof(float))+2*1024*1024;
    return b;
}

// Initialize only this sandbox's stream and fields, rolling back every partially allocated member.
void initializeGpu3D(GPUSimData3D& g, const SimulationContext3D& c,bool cpu) {
    if (g.stream3D||g.field3D[0]) throw std::logic_error("Seis3D: release the previous session before initializing.");
    g.cpu3D=cpu;
    const auto budget=estimateMemory3D(c);
    if(!cpu) {
    int device=0;cudaDeviceProp properties{};
    checkCuda3D(cudaGetDevice(&device),"query current device");
    checkCuda3D(cudaGetDeviceProperties(&properties,device),"query grid launch limits");
    const int launchSize[3]={(c.gx3D-8+31)/32,(c.gy3D-8+3)/4,(c.gz3D-8+1)/2};
    for(int axis=0;axis<3;++axis) if(launchSize[axis]>properties.maxGridSize[axis])
        throw std::invalid_argument("Seis3D: a padded dimension exceeds this CUDA device's launch limit.");
    std::size_t freeBytes=0,totalBytes=0;
    checkCuda3D(cudaMemGetInfo(&freeBytes,&totalBytes),"query VRAM");
    const std::size_t reserve=(std::max)(std::size_t(512)*1024*1024,totalBytes/5);
    if (budget.totalBytes3D>memoryLimit3D || freeBytes<reserve || budget.totalBytes3D>freeBytes-reserve)
        throw std::runtime_error("Seis3D: VRAM budget exceeded; reduce grid size or CPML thickness.");
    }
    try {
        if(!cpu) {
        checkCuda3D(cudaStreamCreateWithFlags(&g.stream3D,cudaStreamNonBlocking),"create stream");
        checkCuda3D(cudaEventCreate(&g.startEvent3D),"create start event");
        checkCuda3D(cudaEventCreate(&g.stopEvent3D),"create stop event");
        }
        for (auto& field:g.field3D) allocateFloats3D(g,field,c.cells3D,g.stream3D);
        allocateFloats3D(g,g.inverseDensity3D,c.cells3D,g.stream3D);
        allocateFloats3D(g,g.lambda3D,c.cells3D,g.stream3D); allocateFloats3D(g,g.mu3D,c.cells3D,g.stream3D);
        for (int a=0;a<3 && !c.reflecting3D;++a) {
            g.slabCells3D[a]=slabCount3D(c,a);
            allocateFloats3D(g,g.cpml3D[a],g.slabCells3D[a]*6,g.stream3D);
            const auto coefficients=makeCoefficients3D(c,a);
            allocateFloats3D(g,g.coefficients3D[a],coefficients.size(),g.stream3D);
            checkCuda3D(copyBackend3D(g,g.coefficients3D[a],coefficients.data(),coefficients.size()*sizeof(float),
                cudaMemcpyHostToDevice,g.stream3D),"upload CPML coefficients");
            checkCuda3D(syncBackend3D(g,g.stream3D),"finish coefficient upload");
        }
        checkCuda3D(allocateBackend3D(g,&g.viewStatistics3D,5*sizeof(unsigned int)),"allocate statistics");
        checkCuda3D(zeroBackend3D(g,g.viewStatistics3D,0,5*sizeof(unsigned int),g.stream3D),"clear statistics");
        if(cpu) generateCpuMaterials3D(g,c);
        else {generateMaterialsKernel3D<<<unsigned((c.cells3D+255)/256),256,0,g.stream3D>>>(g,c); checkLaunch3D();}
        checkCuda3D(syncBackend3D(g,g.stream3D),"initialize model");
        g.allocatedBytes3D=budget.fieldBytes3D+budget.cpmlBytes3D;
    } catch (...) { releaseGpu3D(g); throw; }
}

// Release an entire independent CUDA lifetime without resetting the device or touching 2-D state.
void releaseGpu3D(GPUSimData3D& g) noexcept {
    if (g.stream3D) syncBackend3D(g,g.stream3D);
    for (auto p:g.field3D) freeBackend3D(g,p);
    freeBackend3D(g,g.inverseDensity3D); freeBackend3D(g,g.lambda3D); freeBackend3D(g,g.mu3D);
    for (int a=0;a<3;++a) { freeBackend3D(g,g.cpml3D[a]); freeBackend3D(g,g.coefficients3D[a]); }
    freeBackend3D(g,g.receivers3D); freeBackend3D(g,g.receiverValues3D); freeBackend3D(g,g.viewStatistics3D);
    if (g.hostReceiverValues3D) freeHostBackend3D(g,g.hostReceiverValues3D);
    if (g.startEvent3D) cudaEventDestroy(g.startEvent3D);
    if (g.stopEvent3D) cudaEventDestroy(g.stopEvent3D);
    if (g.stream3D) cudaStreamDestroy(g.stream3D);
    g=GPUSimData3D{};
}

// Clear fields and memory variables, leaving the model and observation geometry intact.
void resetGpu3D(GPUSimData3D& g, const SimulationContext3D& c) {
    for (auto p:g.field3D) checkCuda3D(zeroBackend3D(g,p,0,c.cells3D*sizeof(float),g.stream3D),"reset wavefield");
    for (int a=0;a<3;++a) if(g.slabCells3D[a])
        checkCuda3D(zeroBackend3D(g,g.cpml3D[a],0,g.slabCells3D[a]*6*sizeof(float),g.stream3D),"reset CPML");
    checkCuda3D(zeroBackend3D(g,g.viewStatistics3D,0,5*sizeof(unsigned int),g.stream3D),"reset diagnostics");
}

// Prepare all profiles before committing new frequency-dependent coefficients to the existing stream.
void refreshCpmlCoefficients3D(GPUSimData3D& g,const SimulationContext3D& c) {
    if(!g.stream3D&&!g.cpu3D) throw std::logic_error("Seis3D: simulation not initialized.");
    if(c.reflecting3D) return;
    std::array<std::vector<float>,3> profiles;
    for(int axis=0;axis<3;++axis) profiles[axis]=makeCoefficients3D(c,axis);
    float* replacement[3]{};
    try {
        for(int axis=0;axis<3;++axis) {
            allocateFloats3D(g,replacement[axis],profiles[axis].size(),g.stream3D);
            checkCuda3D(copyBackend3D(g,replacement[axis],profiles[axis].data(),profiles[axis].size()*sizeof(float),
                cudaMemcpyHostToDevice,g.stream3D),"refresh CPML coefficients");
        }
        checkCuda3D(syncBackend3D(g,g.stream3D),"finish CPML refresh");
    } catch(...) {
        syncBackend3D(g,g.stream3D);
        for(auto pointer:replacement) freeBackend3D(g,pointer);
        throw;
    }
    for(int axis=0;axis<3;++axis) {std::swap(g.coefficients3D[axis],replacement[axis]);freeBackend3D(g,replacement[axis]);}
}

// Enqueue second-order leapfrog updates and stagger-aware planar free-surface ghost maps.
void runStep3D(GPUSimData3D& g, const SimulationContext3D& c, const Source3D& source, std::uint64_t step) {
    if (!g.stream3D&&!g.cpu3D) throw std::logic_error("Seis3D: simulation not initialized.");
    if (source.x3D<1 || source.x3D>=c.nx3D-1 || source.y3D<1 || source.y3D>=c.ny3D-1
        || source.z3D<2 || source.z3D>=c.nz3D-1 || !std::isfinite(source.amplitude3D))
        throw std::invalid_argument("Seis3D: source must be inside the physical volume, at depth >=2.");
    for (float d:source.direction3D) if (!std::isfinite(d)) throw std::invalid_argument("Seis3D: invalid force direction.");
    if(g.cpu3D) {runCpuStep3D(g,c,source,step);return;}
    const dim3 block(32,4,2), grid((c.gx3D-8+31)/32,(c.gy3D-8+3)/4,(c.gz3D-8+1)/2);
    const dim3 surfaceBlock(16,16), surfaceGrid((c.gx3D-8+15)/16,(c.gy3D-8+15)/16);
    if (c.freeSurface3D) { stressGhostKernel3D<<<surfaceGrid,surfaceBlock,0,g.stream3D>>>(g,c); checkLaunch3D(); }
    if (c.reflecting3D) applyReflectingWalls3D(g,c,false);
    velocityKernel3D<<<grid,block,0,g.stream3D>>>(g,c); checkLaunch3D();
    if (source.kind3D==SourceKind3D::Force) {
        sourceKernel3D<<<1,32,0,g.stream3D>>>(g,c,source,double(step)*c.dt3D); checkLaunch3D();
    }
    if (c.freeSurface3D) { velocityGhostKernel3D<<<surfaceGrid,surfaceBlock,0,g.stream3D>>>(g,c); checkLaunch3D(); }
    if (c.reflecting3D) applyReflectingWalls3D(g,c,true);
    stressKernel3D<<<grid,block,0,g.stream3D>>>(g,c); checkLaunch3D();
    if (source.kind3D==SourceKind3D::Explosion) {
        sourceKernel3D<<<1,32,0,g.stream3D>>>(g,c,source,(double(step)+0.5)*c.dt3D); checkLaunch3D();
    }
    if (c.freeSurface3D) { stressGhostKernel3D<<<surfaceGrid,surfaceBlock,0,g.stream3D>>>(g,c); checkLaunch3D(); }
    if (c.reflecting3D) applyReflectingWalls3D(g,c,false);
}

// Extract into a caller-mapped CUDA/GL buffer and return its maximum after stream completion.
void extractViews3D(GPUSimData3D& g, const SimulationContext3D& c, float* out,
    const ViewLayout3D& layout, const int slices[3], int component, int materialComponent, bool volume, float& maximum, float materialRange[2], bool includeBoundary,float* velocityMaximum) {
    if (!out || materialComponent<5 || materialComponent>7 || component<0 || component>7 || slices[0]<0 || slices[0]>=c.nx3D
        || slices[1]<0 || slices[1]>=c.ny3D || slices[2]<0 || slices[2]>=c.nz3D)
        throw std::invalid_argument("Seis3D: invalid view parameters.");
    checkCuda3D(zeroBackend3D(g,g.viewStatistics3D,0,sizeof(unsigned int),g.stream3D),"reset view maximum");
    checkCuda3D(zeroBackend3D(g,g.viewStatistics3D+4,0,sizeof(unsigned int),g.stream3D),"reset common velocity maximum");
    const unsigned int rangeInitial[2]={0x7f7fffffu,0u};
    checkCuda3D(copyBackend3D(g,g.viewStatistics3D+2,rangeInitial,sizeof(rangeInitial),cudaMemcpyHostToDevice,g.stream3D),"reset material range");
    const auto count=volume ? layout.totalSamples3D : layout.volumeOffset3D;
    if(g.cpu3D) extractCpuViews3D(g,c,out,layout,slices,component,materialComponent,volume,includeBoundary);
    else {
    extractKernel3D<<<unsigned((count+255)/256),256,0,g.stream3D>>>(g,c,out,layout,slices[0],slices[1],slices[2],component,materialComponent,volume,includeBoundary);
    checkLaunch3D();
    }
    unsigned int stats[5]{};
    checkCuda3D(copyBackend3D(g,stats,g.viewStatistics3D,sizeof(stats),cudaMemcpyDeviceToHost,g.stream3D),"read diagnostics");
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish view extraction");
    if (stats[1]) throw std::runtime_error("Seis3D: non-finite wavefield detected; session stopped.");
    std::memcpy(&maximum,stats,sizeof(float));
    std::memcpy(materialRange,stats+2,2*sizeof(float));
    if(velocityMaximum) std::memcpy(velocityMaximum,stats+4,sizeof(float));
}

// Validate and atomically replace receiver buffers, including the pinned transfer allocation.
void setReceivers3D(GPUSimData3D& g, const SimulationContext3D& c, const Receiver3D* receivers, int count) {
    if (count<0 || count>maxReceivers3D || (count && !receivers)) throw std::invalid_argument("Seis3D: invalid receiver count.");
    for (int a=0;a<count;++a) {
        const auto p=receivers[a];
        if (p.x3D<0 || p.x3D>=c.nx3D || p.y3D<0 || p.y3D>=c.ny3D || p.z3D<0 || p.z3D>=c.nz3D)
            throw std::invalid_argument("Seis3D: receiver outside the physical domain.");
    }
    Receiver3D* positions=nullptr; float* values=nullptr; float* host=nullptr;
    try {
        if (count) {
            checkCuda3D(allocateBackend3D(g,&positions,std::size_t(count)*sizeof(Receiver3D)),"allocate receivers");
            checkCuda3D(allocateBackend3D(g,&values,std::size_t(count)*3*sizeof(float)),"allocate receiver values");
            checkCuda3D(allocateHostBackend3D(g,&host,std::size_t(count)*3*sizeof(float)),"allocate pinned receivers");
            checkCuda3D(copyBackend3D(g,positions,receivers,std::size_t(count)*sizeof(Receiver3D),cudaMemcpyHostToDevice,g.stream3D),"upload receivers");
        }
        checkCuda3D(syncBackend3D(g,g.stream3D),"finish receiver update");
    } catch (...) { freeBackend3D(g,positions); freeBackend3D(g,values); if (host) freeHostBackend3D(g,host); throw; }
    freeBackend3D(g,g.receivers3D); freeBackend3D(g,g.receiverValues3D); if (g.hostReceiverValues3D) freeHostBackend3D(g,g.hostReceiverValues3D);
    g.receivers3D=positions; g.receiverValues3D=values; g.hostReceiverValues3D=host; g.receiverCount3D=count;
}

// Gather only receiver values, preserving full wavefields on the GPU.
const float* sampleReceivers3D(GPUSimData3D& g, const SimulationContext3D& c) {
    if (!g.receiverCount3D) return nullptr;
    if(g.cpu3D) sampleCpuReceivers3D(g,c);
    else {
    receiverKernel3D<<<(g.receiverCount3D+255)/256,256,0,g.stream3D>>>(g,c); checkLaunch3D();
    }
    checkCuda3D(copyBackend3D(g,g.hostReceiverValues3D,g.receiverValues3D,std::size_t(g.receiverCount3D)*3*sizeof(float),
        cudaMemcpyDeviceToHost,g.stream3D),"gather receivers");
    unsigned int invalid=0;
    checkCuda3D(copyBackend3D(g,&invalid,g.viewStatistics3D+1,sizeof(invalid),cudaMemcpyDeviceToHost,g.stream3D),"check capture stability");
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish receiver gather");
    if (invalid) throw std::runtime_error("Seis3D: non-finite wavefield detected; capture stopped before export.");
    return g.hostReceiverValues3D;
}

// Query just one material node, using the sandbox stream rather than device-wide synchronization.
void probeMaterial3D(GPUSimData3D& g, const SimulationContext3D& c, const Receiver3D& p, float values[3]) {
    if (p.x3D<0 || p.x3D>=c.nx3D || p.y3D<0 || p.y3D>=c.ny3D || p.z3D<0 || p.z3D>=c.nz3D) return;
    const auto i=gridIndex3D(c,p.x3D+c.ox3D,p.y3D+c.oy3D,p.z3D+c.oz3D);
    float inverse=0,mu=0,lambda=0;
    checkCuda3D(copyBackend3D(g,&inverse,g.inverseDensity3D+i,4,cudaMemcpyDeviceToHost,g.stream3D),"probe density");
    checkCuda3D(copyBackend3D(g,&mu,g.mu3D+i,4,cudaMemcpyDeviceToHost,g.stream3D),"probe mu");
    checkCuda3D(copyBackend3D(g,&lambda,g.lambda3D+i,4,cudaMemcpyDeviceToHost,g.stream3D),"probe lambda");
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish material probe");
    values[0]=std::sqrt((lambda+2*mu)*inverse); values[1]=std::sqrt(mu*inverse); values[2]=1/inverse;
}

// Convert one physical plane to Lamé parameters, then upload each material with a pitched copy.
void uploadModelPlane3D(GPUSimData3D& g, const SimulationContext3D& c, int z, const float* values) {
    if (z<0 || z>=c.nz3D || !values) throw std::invalid_argument("Seis3D: invalid model plane.");
    const std::size_t n=std::size_t(c.nx3D)*c.ny3D;
    std::vector<float> material(n*3);
    for (std::size_t i=0;i<n;++i) {
        const float vp=values[3*i],vs=values[3*i+1],rho=values[3*i+2];
        if (!validMaterial3D(vp,vs,rho) || vp>c.maxVp3D*(1+1.0e-6f))
            throw std::invalid_argument("Seis3D: invalid or changed imported material.");
        material[i]=1/rho; material[n+i]=rho*(vp*vp-2*vs*vs); material[2*n+i]=rho*vs*vs;
    }
    const auto offset=gridIndex3D(c,c.ox3D,c.oy3D,z+c.oz3D);
    float* destinations[3]={g.inverseDensity3D,g.lambda3D,g.mu3D};
    for (int a=0;a<3;++a) checkCuda3D(copyPlaneBackend3D(g,destinations[a]+offset,std::size_t(c.gx3D)*4,
        material.data()+a*n,std::size_t(c.nx3D)*4,std::size_t(c.nx3D)*4,c.ny3D,cudaMemcpyHostToDevice,g.stream3D),"upload material plane");
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish material plane");
}

// Extend physical edge cells to all padding before the first simulation step.
void finishModelUpload3D(GPUSimData3D& g, const SimulationContext3D& c) {
    if(g.cpu3D) generateCpuMaterials3D(g,c,true);
    else {extendMaterialsKernel3D<<<unsigned((c.cells3D+255)/256),256,0,g.stream3D>>>(g,c); checkLaunch3D();}
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish model import");
}

// Download a material plane with bounded CPU memory and convert Lamé values back to SI materials.
void downloadModelPlane3D(GPUSimData3D& g, const SimulationContext3D& c, int z, float* values) {
    if (z<0 || z>=c.nz3D || !values) throw std::invalid_argument("Seis3D: invalid export plane.");
    const std::size_t n=std::size_t(c.nx3D)*c.ny3D;
    std::vector<float> material(n*3);
    const auto offset=gridIndex3D(c,c.ox3D,c.oy3D,z+c.oz3D);
    const float* sources[3]={g.inverseDensity3D,g.lambda3D,g.mu3D};
    for (int a=0;a<3;++a) checkCuda3D(copyPlaneBackend3D(g,material.data()+a*n,std::size_t(c.nx3D)*4,
        sources[a]+offset,std::size_t(c.gx3D)*4,std::size_t(c.nx3D)*4,c.ny3D,cudaMemcpyDeviceToHost,g.stream3D),"download material plane");
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish model plane export");
    for (std::size_t i=0;i<n;++i) {
        values[3*i]=std::sqrt((material[n+i]+2*material[2*n+i])*material[i]);
        values[3*i+1]=std::sqrt(material[2*n+i]*material[i]); values[3*i+2]=1/material[i];
    }
}

// Download only the field planes needed for the viewer's staggered-grid collocation.
void downloadWavefieldPlane3D(GPUSimData3D& g,const SimulationContext3D& c,int z,int component,float* values,float* scratch) {
    if((!g.stream3D&&!g.cpu3D)||z<0||z>=c.nz3D||component<0||component>4||!values||!scratch)
        throw std::invalid_argument("Seis3D: invalid wavefield export plane or component.");
    const std::size_t count=std::size_t(c.nx3D)*c.ny3D;
    const std::size_t hostPitch=std::size_t(c.nx3D)*sizeof(float),devicePitch=std::size_t(c.gx3D)*sizeof(float);
    struct PlaneSource3D { int field3D,dx3D,dy3D,dz3D; } sources3D[6]{};
    int sourceCount3D=0;
    auto addSource3D=[&](int field3D,int dx3D,int dy3D,int dz3D){sources3D[sourceCount3D++]={field3D,dx3D,dy3D,dz3D};};
    if(component==0) { addSource3D(2,0,0,0);addSource3D(2,0,0,-1); }
    else if(component==1) { addSource3D(0,0,0,0);addSource3D(0,-1,0,0); }
    else if(component==2) { addSource3D(1,0,0,0);addSource3D(1,0,-1,0); }
    else if(component==3) {
        addSource3D(0,0,0,0);addSource3D(0,-1,0,0);
        addSource3D(1,0,0,0);addSource3D(1,0,-1,0);
        addSource3D(2,0,0,0);addSource3D(2,0,0,-1);
    } else { addSource3D(3,0,0,0);addSource3D(4,0,0,0);addSource3D(5,0,0,0); }
    for(int sourceIndex3D=0;sourceIndex3D<sourceCount3D;++sourceIndex3D) {
        const auto& source3D=sources3D[sourceIndex3D];
        const auto offset3D=gridIndex3D(c,c.ox3D+source3D.dx3D,c.oy3D+source3D.dy3D,c.oz3D+z+source3D.dz3D);
        checkCuda3D(copyPlaneBackend3D(g,scratch+std::size_t(sourceIndex3D)*count,hostPitch,
            g.field3D[source3D.field3D]+offset3D,devicePitch,hostPitch,c.ny3D,cudaMemcpyDeviceToHost,g.stream3D),
            "download wavefield plane");
    }
    checkCuda3D(syncBackend3D(g,g.stream3D),"finish wavefield plane export");
    for(std::size_t i3D=0;i3D<count;++i3D) {
        const float* a3D=scratch;
        if(component==0||component==1||component==2) values[i3D]=.5f*(a3D[i3D]+scratch[count+i3D]);
        else if(component==3) {
            const float vx3D=.5f*(scratch[i3D]+scratch[count+i3D]);
            const float vy3D=.5f*(scratch[2*count+i3D]+scratch[3*count+i3D]);
            const float vz3D=.5f*(scratch[4*count+i3D]+scratch[5*count+i3D]);
            values[i3D]=std::sqrt(vx3D*vx3D+vy3D*vy3D+vz3D*vz3D);
        } else values[i3D]=-(scratch[i3D]+scratch[count+i3D]+scratch[2*count+i3D])/3.0f;
    }
}

} // namespace Seis3D
