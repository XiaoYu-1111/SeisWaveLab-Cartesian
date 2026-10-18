#include "Cuda_Seis3D.cuh"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace Seis3D {
namespace {

// Translate CUDA failures into sandbox-local exceptions, with the failing operation named.
void checkCuda3D(cudaError_t result, const char* operation) {
    if (result != cudaSuccess)
        throw std::runtime_error(std::string("Seis3D / ") + operation + ": " + cudaGetErrorString(result));
}

// Check the next asynchronous kernel launch without synchronizing the entire device.
void checkLaunch3D() { checkCuda3D(cudaGetLastError(), "kernel launch"); }

// Allocate and clear one stream-owned float buffer; caller owns rollback on failure.
void allocateFloats3D(float*& pointer, std::size_t count, cudaStream_t stream) {
    if (count == 0) return;
    checkCuda3D(cudaMalloc(&pointer, count * sizeof(float)), "allocate field");
    checkCuda3D(cudaMemsetAsync(pointer, 0, count * sizeof(float), stream), "clear field");
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
__device__ __forceinline__ std::size_t index3D(const SimulationContext3D& c, int x, int y, int z) {
    return (std::size_t(z) * c.gy3D + y) * c.gx3D + x;
}

// Return eighth-order staggered first-derivative weights, shared by all three axes.
__device__ __forceinline__ float weight3D(int r) {
    return r == 0 ? 1225.0f / 1024.0f : r == 1 ? -245.0f / 3072.0f
        : r == 2 ? 49.0f / 5120.0f : -5.0f / 7168.0f;
}

// Evaluate D+ at i+1/2 or D- at i, using eight samples along the selected axis.
template<bool plus>
__device__ __forceinline__ float derivative3D(const float* field, std::size_t i,
    std::size_t stride, float inverseSpacing) {
    float result = 0;
    #pragma unroll
    for (int r = 0; r < 4; ++r) {
        const std::size_t positive = i + std::size_t(r + (plus ? 1 : 0)) * stride;
        const std::size_t negative = i - std::size_t(r + (plus ? 0 : 1)) * stride;
        result += weight3D(r) * (field[positive] - field[negative]);
    }
    return result * inverseSpacing;
}

// Apply one derivative's CPML recurrence in its direction's compact slab allocation.
__device__ __forceinline__ float applyCpml3D(const GPUSimData3D& g, const SimulationContext3D& c,
    int x, int y, int z, int axis, int slot, bool half, float derivative) {
    if (c.reflecting3D) return derivative;
    const int n = axis == 0 ? c.gx3D : axis == 1 ? c.gy3D : c.gz3D;
    const int coordinate = axis == 0 ? x : axis == 1 ? y : z;
    const bool topFree = axis == 2 && c.freeSurface3D;
    int q = -1;
    if (!topFree && coordinate < stencilRadius3D + c.pml3D) q = coordinate - stencilRadius3D;
    else if (coordinate >= n - stencilRadius3D - c.pml3D)
        q = (topFree ? 0 : c.pml3D) + coordinate - (n - stencilRadius3D - c.pml3D);
    if (q < 0) return derivative;
    const int thickness = (topFree ? 1 : 2) * c.pml3D;
    const std::size_t slab = axis == 0 ? (std::size_t(z) * c.gy3D + y) * thickness + q
        : axis == 1 ? (std::size_t(z) * c.gx3D + x) * thickness + q
        : (std::size_t(y) * c.gx3D + x) * thickness + q;
    float& memory = g.cpml3D[axis][std::size_t(slot) * g.slabCells3D[axis] + slab];
    const float* coefficient = g.coefficients3D[axis] + (int(half) * n + coordinate) * 3;
    memory = coefficient[1] * memory + coefficient[0] * derivative;
    return coefficient[2] * derivative + memory;
}

// Populate a procedural model directly on the GPU, extending material properties into padding.
__global__ void generateMaterialsKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const std::size_t i = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= c.cells3D) return;
    // Extend the nearest physical material into padding, including dipping and graded models.
    const int x = max(0,min(c.nx3D-1,int(i % c.gx3D) - c.ox3D));
    const int y = max(0,min(c.ny3D-1,int((i / c.gx3D) % c.gy3D) - c.oy3D));
    const int z = max(0,min(c.nz3D-1,int(i / (std::size_t(c.gx3D) * c.gy3D)) - c.oz3D));
    const float dx = (x - c.nx3D * 0.5f) / c.nx3D;
    const float dy = (y - c.ny3D * 0.5f) / c.ny3D;
    const float dz = (z - c.nz3D * 0.55f) / c.nz3D;
    const float depth = float(z)/float(c.nz3D-1), horizontal = float(x)/float(c.nx3D-1)-0.5f;
    float blend = 0;
    switch (c.modelKind3D) {
    case ModelKind3D::TwoLayer: blend = depth >= c.interfaceDepth3D; break;
    case ModelKind3D::Sphere: blend = dx*dx+dy*dy+dz*dz < c.anomalyRadius3D*c.anomalyRadius3D; break;
    case ModelKind3D::ThreeLayer: blend = depth < 1.0f/3 ? 0 : depth < 2.0f/3 ? 0.5f : 1; break;
    case ModelKind3D::DippingLayer: blend = depth >= c.interfaceDepth3D+c.layerDip3D*horizontal; break;
    case ModelKind3D::LowVelocityLayer: blend = fabsf(depth-c.interfaceDepth3D) <= 0.5f*c.layerThickness3D; break;
    case ModelKind3D::Fault: blend = depth >= c.interfaceDepth3D+c.layerDip3D*horizontal
        +(horizontal>0 ? c.faultThrow3D : 0); break;
    case ModelKind3D::Gradient: blend = depth; break;
    case ModelKind3D::SaltDome: {
        const float vertical = (depth-0.78f)/0.52f;
        blend = (dx*dx+dy*dy)/(c.anomalyRadius3D*c.anomalyRadius3D)+vertical*vertical < 1;
        break;
    }
    default: break;
    }
    const float vp = c.vp3D+blend*(c.layerVp3D-c.vp3D), vs = c.vs3D+blend*(c.layerVs3D-c.vs3D);
    const float rho = c.density3D+blend*(c.layerDensity3D-c.density3D);
    g.inverseDensity3D[i] = 1.0f / rho;
    g.mu3D[i] = rho * vs * vs;
    g.lambda3D[i] = rho * (vp * vp - 2.0f * vs * vs);
}

// Mirror one pair of cell-face slip walls with stagger-aware parity. Axis launches are ordered
// on the same stream, so edges/corners inherit the product of the independent face reflections.
// Normal velocity is odd, tangential velocity even; normal stress is even, shear traction odd.
__global__ void reflectingGhostKernel3D(GPUSimData3D g, SimulationContext3D c, int axis, bool velocities) {
    const int lengths[3] = {c.gx3D,c.gy3D,c.gz3D};
    const int uAxis = (axis+1)%3, vAxis = (axis+2)%3;
    const int u = int(blockIdx.x*blockDim.x+threadIdx.x);
    const int v = int(blockIdx.y*blockDim.y+threadIdx.y);
    if (u >= lengths[uAxis] || v >= lengths[vAxis]) return;
    const int high = lengths[axis]-stencilRadius3D-1;
    int p[3]{}; p[uAxis]=u; p[vAxis]=v;
    for (int field=velocities?0:3; field<(velocities?3:9); ++field) {
        const bool half = field<3 ? field==axis : field==6 ? axis!=2 : field==7 ? axis!=0 : field==8 ? axis!=1 : false;
        const bool odd = half; // Half-grid fields are exactly vn and sigma_nt for this face.
        for (int side=0; side<2; ++side) for (int depth=1; depth<=stencilRadius3D; ++depth) {
            p[axis] = side ? high+depth : stencilRadius3D-depth;
            const auto destination = index3D(c,p[0],p[1],p[2]);
            if (half && !side && depth==1) {g.field3D[field][destination]=0; continue;}
            p[axis] = side ? high+1-depth-int(half) : stencilRadius3D+depth-1-int(half);
            const float value = g.field3D[field][index3D(c,p[0],p[1],p[2])];
            g.field3D[field][destination] = odd ? -value : value;
        }
        if (half) { // The upper half-grid face is an active velocity/shear node.
            p[axis]=high; g.field3D[field][index3D(c,p[0],p[1],p[2])]=0;
        }
    }
}

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
__global__ void extendMaterialsKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const std::size_t i = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= c.cells3D) return;
    const int x = int(i % c.gx3D), y = int((i / c.gx3D) % c.gy3D);
    const int z = int(i / (std::size_t(c.gx3D) * c.gy3D));
    const int px = max(c.ox3D, min(x, c.ox3D + c.nx3D - 1));
    const int py = max(c.oy3D, min(y, c.oy3D + c.ny3D - 1));
    const int pz = max(c.oz3D, min(z, c.oz3D + c.nz3D - 1));
    if (x == px && y == py && z == pz) return;
    const auto j = index3D(c, px, py, pz);
    g.inverseDensity3D[i] = g.inverseDensity3D[j];
    g.mu3D[i] = g.mu3D[j]; g.lambda3D[i] = g.lambda3D[j];
}

// Fill traction ghost images: szz is odd at integer z=0; sxz/syz are odd at half-grid z.
__global__ void stressGhostKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const int x = int(blockIdx.x * blockDim.x + threadIdx.x) + 4;
    const int y = int(blockIdx.y * blockDim.y + threadIdx.y) + 4;
    if (x >= c.gx3D - 4 || y >= c.gy3D - 4) return;
    g.field3D[5][index3D(c, x, y, 4)] = 0;
    for (int depth = 1; depth <= 4; ++depth) {
        const auto ghost = index3D(c, x, y, 4 - depth);
        const auto mirror = index3D(c, x, y, 4 + depth);
        const auto shearMirror = index3D(c, x, y, 3 + depth);
        g.field3D[5][ghost] = -g.field3D[5][mirror];
        g.field3D[8][ghost] = -g.field3D[8][shearMirror];
        g.field3D[7][ghost] = -g.field3D[7][shearMirror];
        g.field3D[3][ghost] = g.field3D[3][mirror];
        g.field3D[4][ghost] = g.field3D[4][mirror];
        g.field3D[6][ghost] = g.field3D[6][mirror];
    }
}

// Update half-time velocities at their respective face-centered density locations.
__global__ void velocityKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const int x = int(blockIdx.x * blockDim.x + threadIdx.x) + 4;
    const int y = int(blockIdx.y * blockDim.y + threadIdx.y) + 4;
    const int z = int(blockIdx.z * blockDim.z + threadIdx.z) + 4;
    if (x >= c.gx3D - 4 || y >= c.gy3D - 4 || z >= c.gz3D - 4) return;
    const auto i = index3D(c, x, y, z);
    const std::size_t strides[3] = { 1, std::size_t(c.gx3D), std::size_t(c.gx3D) * c.gy3D };
    const int stressFields[3][3] = { {3,6,8}, {6,4,7}, {8,7,5} };
    #pragma unroll
    for (int v = 0; v < 3; ++v) {
        float divergence = 0;
        #pragma unroll
        for (int axis = 0; axis < 3; ++axis) {
            const float d = axis == v
                ? derivative3D<true>(g.field3D[stressFields[v][axis]], i, strides[axis], 1.0f / c.spacing3D)
                : derivative3D<false>(g.field3D[stressFields[v][axis]], i, strides[axis], 1.0f / c.spacing3D);
            divergence += applyCpml3D(g, c, x, y, z, axis, v, axis == v, d);
        }
        const float a = g.inverseDensity3D[i], b = g.inverseDensity3D[i + strides[v]];
        const float value = g.field3D[v][i] + c.dt3D * (2 * a * b / (a + b)) * divergence;
        g.field3D[v][i] = value;
        if (!isfinite(value)) atomicExch(g.viewStatistics3D + 1, 1u);
    }
}

// Map velocity ghosts using the three zero-traction differential constraints at z=0.
// Interior differences remain eighth order; this planar surface closure is second order.
__global__ void velocityGhostKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const int x = int(blockIdx.x * blockDim.x + threadIdx.x) + 4;
    const int y = int(blockIdx.y * blockDim.y + threadIdx.y) + 4;
    if (x >= c.gx3D - 4 || y >= c.gy3D - 4) return;
    const auto i = index3D(c, x, y, 4);
    const float invH = 1.0f / c.spacing3D;
    const float ex = derivative3D<false>(g.field3D[0], i, 1, invH);
    const float ey = derivative3D<false>(g.field3D[1], i, c.gx3D, invH);
    const auto nextPlane = i + std::size_t(c.gx3D) * c.gy3D;
    // Second-order extrapolation from vz at z=h/2 and 3h/2 to its surface value.
    const float dzVx = -(1.5f * derivative3D<true>(g.field3D[2], i, 1, invH)
        - 0.5f * derivative3D<true>(g.field3D[2], nextPlane, 1, invH));
    const float dzVy = -(1.5f * derivative3D<true>(g.field3D[2], i, c.gx3D, invH)
        - 0.5f * derivative3D<true>(g.field3D[2], nextPlane, c.gx3D, invH));
    const float dzVz = -g.lambda3D[i] / (g.lambda3D[i] + 2 * g.mu3D[i]) * (ex + ey);
    for (int depth = 1; depth <= 4; ++depth) {
        const auto ghost = index3D(c, x, y, 4 - depth);
        const auto mirror = index3D(c, x, y, 4 + depth);
        g.field3D[0][ghost] = g.field3D[0][mirror] - 2 * depth * c.spacing3D * dzVx;
        g.field3D[1][ghost] = g.field3D[1][mirror] - 2 * depth * c.spacing3D * dzVy;
        g.field3D[2][ghost] = g.field3D[2][index3D(c, x, y, 3 + depth)]
            - (2 * depth - 1) * c.spacing3D * dzVz;
    }
}

// Harmonic four-cell shear averaging preserves zero shear modulus on fluid interfaces.
__device__ __forceinline__ float shearMu3D(const float* mu, std::size_t i, std::size_t a, std::size_t b) {
    const float m0 = mu[i], m1 = mu[i+a], m2 = mu[i+b], m3 = mu[i+a+b];
    return (m0 > 0 && m1 > 0 && m2 > 0 && m3 > 0) ? 4 / (1/m0 + 1/m1 + 1/m2 + 1/m3) : 0;
}

// Update the six stresses using independently filtered derivatives and an unsplit constitutive law.
__global__ void stressKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const int x = int(blockIdx.x * blockDim.x + threadIdx.x) + 4;
    const int y = int(blockIdx.y * blockDim.y + threadIdx.y) + 4;
    const int z = int(blockIdx.z * blockDim.z + threadIdx.z) + 4;
    if (x >= c.gx3D - 4 || y >= c.gy3D - 4 || z >= c.gz3D - 4) return;
    const auto i = index3D(c, x, y, z);
    const std::size_t stride[3] = { 1, std::size_t(c.gx3D), std::size_t(c.gx3D) * c.gy3D };
    float d[3][3]; // d[velocity component][derivative axis].
    #pragma unroll
    for (int v = 0; v < 3; ++v) {
        #pragma unroll
        for (int a = 0; a < 3; ++a) {
            const float raw = a == v ? derivative3D<false>(g.field3D[v], i, stride[a], 1/c.spacing3D)
                : derivative3D<true>(g.field3D[v], i, stride[a], 1/c.spacing3D);
            d[v][a] = applyCpml3D(g, c, x, y, z, a, 3+v, a != v, raw);
        }
    }
    const float lambda = g.lambda3D[i], mu = g.mu3D[i];
    if (c.freeSurface3D && z == 4) d[2][2] = -lambda / (lambda + 2*mu) * (d[0][0] + d[1][1]);
    const float trace = d[0][0] + d[1][1] + d[2][2];
    g.field3D[3][i] += c.dt3D * (lambda * trace + 2 * mu * d[0][0]);
    g.field3D[4][i] += c.dt3D * (lambda * trace + 2 * mu * d[1][1]);
    g.field3D[5][i] = c.freeSurface3D && z == 4 ? 0
        : g.field3D[5][i] + c.dt3D * (lambda * trace + 2 * mu * d[2][2]);
    g.field3D[6][i] += c.dt3D * shearMu3D(g.mu3D, i, stride[0], stride[1]) * (d[0][1] + d[1][0]);
    g.field3D[7][i] += c.dt3D * shearMu3D(g.mu3D, i, stride[1], stride[2]) * (d[1][2] + d[2][1]);
    g.field3D[8][i] += c.dt3D * shearMu3D(g.mu3D, i, stride[0], stride[2]) * (d[0][2] + d[2][0]);
    #pragma unroll
    for (int a = 3; a < 9; ++a) if (!isfinite(g.field3D[a][i])) atomicExch(g.viewStatistics3D + 1, 1u);
}

// Inject a normalized 3x3x3 Ricker source at the correct velocity/stress leapfrog time.
__global__ void sourceKernel3D(GPUSimData3D g, SimulationContext3D c, Source3D source, double time) {
    const int a = threadIdx.x;
    if (a >= 27) return;
    const int dx = a % 3 - 1, dy = (a / 3) % 3 - 1, dz = a / 9 - 1;
    const int x = source.x3D + c.ox3D + dx, y = source.y3D + c.oy3D + dy, z = source.z3D + c.oz3D + dz;
    const auto i = index3D(c, x, y, z);
    const double phase = 3.141592653589793 * c.frequency3D * (time - 1.2 / c.frequency3D);
    const float ricker = float((1 - 2*phase*phase) * exp(-phase*phase));
    const float norm = 10.8387785335f; // (1 + 2 exp(-1/2))^3.
    const float value = c.dt3D * source.amplitude3D * ricker * expf(-0.5f * (dx*dx+dy*dy+dz*dz)) / norm;
    if (source.kind3D == SourceKind3D::Explosion) {
        g.field3D[3][i] += value; g.field3D[4][i] += value; g.field3D[5][i] += value;
    } else {
        const std::size_t strides[3]={1,std::size_t(c.gx3D),std::size_t(c.gx3D)*c.gy3D};
        for (int v = 0; v < 3; ++v) {
            const float a=g.inverseDensity3D[i],b=g.inverseDensity3D[i+strides[v]];
            g.field3D[v][i] += value * source.direction3D[v] * (2*a*b/(a+b));
        }
    }
}

// Collocate face velocities at the cell center and return the selected physical scalar.
__device__ float sampleScalar3D(const GPUSimData3D& g, const SimulationContext3D& c,
    int x, int y, int z, int component,float* velocityMagnitude=nullptr) {
    const auto i = index3D(c, x+c.ox3D, y+c.oy3D, z+c.oz3D);
    if (component == 5) return sqrtf((g.lambda3D[i]+2*g.mu3D[i])*g.inverseDensity3D[i]);
    if (component == 6) return sqrtf(g.mu3D[i]*g.inverseDensity3D[i]);
    if (component == 7) return 1/g.inverseDensity3D[i];
    const float vx = 0.5f*(g.field3D[0][i]+g.field3D[0][i-1]);
    const float vy = 0.5f*(g.field3D[1][i]+g.field3D[1][i-c.gx3D]);
    const float vz = 0.5f*(g.field3D[2][i]+g.field3D[2][i-std::size_t(c.gx3D)*c.gy3D]);
    const float magnitude=sqrtf(vx*vx+vy*vy+vz*vz);
    if(velocityMagnitude) *velocityMagnitude=magnitude;
    if (component == 0) return vz;
    if (component == 1) return vx;
    if (component == 2) return vy;
    if (component == 3) return magnitude;
    if (component == 4) return -(g.field3D[3][i]+g.field3D[4][i]+g.field3D[5][i])/3;
    return 0;
}

// Resample only the bounded display volume at cell centers; solver fields and slice data stay intact.
__device__ float resampleScalar3D(const GPUSimData3D& g,const SimulationContext3D& c,
    float x,float y,float z,int component,float* velocityMagnitude=nullptr) {
    const int ix=int(floorf(x)),iy=int(floorf(y)),iz=int(floorf(z));
    const float fraction[3]={x-ix,y-iy,z-iz};float result=0;
    for(int dz=0;dz<2;++dz) for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
        const float weight=(dx?fraction[0]:1-fraction[0])*(dy?fraction[1]:1-fraction[1])*(dz?fraction[2]:1-fraction[2]);
        if(weight>0) {
            float magnitude=0;result+=weight*sampleScalar3D(g,c,ix+dx,iy+dy,iz+dz,component,velocityMagnitude?&magnitude:nullptr);
            if(velocityMagnitude) *velocityMagnitude+=weight*magnitude;
        }
    }
    return result;
}

// Extract a packed slice or the auxiliary volume, retaining a physical-unit common color maximum.
__global__ void extractKernel3D(GPUSimData3D g, SimulationContext3D c, float* output,
    ViewLayout3D layout, int sx, int sy, int sz, int component, int materialComponent, bool volume, bool includeBoundary) {
    const std::size_t i = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::size_t limit = volume ? layout.totalSamples3D : layout.volumeOffset3D;
    if (i >= limit) return;
    int x, y, z;float fx=0,fy=0,fz=0;
    const int nx=layout.domainSize3D[0],ny=layout.domainSize3D[1],nz=layout.domainSize3D[2];
    const int ox=layout.domainOrigin3D[0],oy=layout.domainOrigin3D[1],oz=layout.domainOrigin3D[2];
    if (i < layout.sliceOffset3D[1]) { x = int(i % nx)+ox; y = sy; z = int(i / nx)+oz; }
    else if (i < layout.sliceOffset3D[2]) {
        const auto j = i-layout.sliceOffset3D[1]; x = sx; y = int(j % ny)+oy; z = int(j/ny)+oz;
    } else if (i < layout.volumeOffset3D) {
        const auto j = i-layout.sliceOffset3D[2]; x = int(j % nx)+ox; y = int(j/nx)+oy; z = sz;
    } else {
        const auto j = i-layout.volumeOffset3D;
        fx=(float(j%layout.volumeX3D)+.5f)*float(nx)/layout.volumeX3D-.5f+ox;
        fy=(float((j/layout.volumeX3D)%layout.volumeY3D)+.5f)*float(ny)/layout.volumeY3D-.5f+oy;
        fz=(float(j/(std::size_t(layout.volumeX3D)*layout.volumeY3D))+.5f)*float(nz)/layout.volumeZ3D-.5f+oz;
        x=int(floorf(fx));y=int(floorf(fy));z=int(floorf(fz));
    }
    const bool volumeSample=i>=layout.volumeOffset3D;
    float magnitude=0;
    const float value = volumeSample?resampleScalar3D(g,c,fx,fy,fz,component,&magnitude):sampleScalar3D(g,c,x,y,z,component,&magnitude);
    const float material = volumeSample?resampleScalar3D(g,c,fx,fy,fz,materialComponent):sampleScalar3D(g,c,x,y,z,materialComponent);
    output[2*i] = isfinite(value) ? value : 0;
    output[2*i+1] = isfinite(material) ? material : 0;
    if (!isfinite(value) || !isfinite(material) || !isfinite(magnitude) || material < 0) atomicExch(g.viewStatistics3D+1, 1u);
    // Reduce within each active warp before atomics, including the final partially populated warp.
    const bool physical=x>=0&&x<c.nx3D&&y>=0&&y<c.ny3D&&z>=0&&z<c.nz3D;
    float waveMax=(physical||includeBoundary)&&isfinite(value)?fabsf(value):0;
    float velocityMax=(physical||includeBoundary)&&isfinite(magnitude)?magnitude:0;
    float materialMin=physical&&isfinite(material)?material:3.402823466e38f;
    float materialMax=physical&&isfinite(material)?material:0;
    const unsigned mask=__activemask();
    const int lane=threadIdx.x&31, active=__popc(mask);
    for(int offset=16;offset>0;offset/=2) {
        const float nextWave=__shfl_down_sync(mask,waveMax,offset);
        const float nextVelocity=__shfl_down_sync(mask,velocityMax,offset);
        const float nextMin=__shfl_down_sync(mask,materialMin,offset);
        const float nextMax=__shfl_down_sync(mask,materialMax,offset);
        if(lane+offset<active) {waveMax=fmaxf(waveMax,nextWave);velocityMax=fmaxf(velocityMax,nextVelocity);materialMin=fminf(materialMin,nextMin);materialMax=fmaxf(materialMax,nextMax);}
    }
    if(lane==0) {
        atomicMax(g.viewStatistics3D,__float_as_uint(waveMax));
        atomicMin(g.viewStatistics3D+2,__float_as_uint(materialMin));
        atomicMax(g.viewStatistics3D+3,__float_as_uint(materialMax));
        atomicMax(g.viewStatistics3D+4,__float_as_uint(velocityMax));
    }
}

// Gather receiver velocities into a compact interleaved buffer, validating coordinates on upload.
__global__ void receiverKernel3D(GPUSimData3D g, SimulationContext3D c) {
    const int r = int(blockIdx.x * blockDim.x + threadIdx.x);
    if (r >= g.receiverCount3D) return;
    const auto p = g.receivers3D[r];
    g.receiverValues3D[3*r] = sampleScalar3D(g,c,p.x3D,p.y3D,p.z3D,1);
    g.receiverValues3D[3*r+1] = sampleScalar3D(g,c,p.x3D,p.y3D,p.z3D,2);
    g.receiverValues3D[3*r+2] = sampleScalar3D(g,c,p.x3D,p.y3D,p.z3D,0);
}

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
void initializeGpu3D(GPUSimData3D& g, const SimulationContext3D& c) {
    if (g.stream3D) throw std::logic_error("Seis3D: release the previous session before initializing.");
    const auto budget=estimateMemory3D(c);
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
    try {
        checkCuda3D(cudaStreamCreateWithFlags(&g.stream3D,cudaStreamNonBlocking),"create stream");
        checkCuda3D(cudaEventCreate(&g.startEvent3D),"create start event");
        checkCuda3D(cudaEventCreate(&g.stopEvent3D),"create stop event");
        for (auto& field:g.field3D) allocateFloats3D(field,c.cells3D,g.stream3D);
        allocateFloats3D(g.inverseDensity3D,c.cells3D,g.stream3D);
        allocateFloats3D(g.lambda3D,c.cells3D,g.stream3D); allocateFloats3D(g.mu3D,c.cells3D,g.stream3D);
        for (int a=0;a<3 && !c.reflecting3D;++a) {
            g.slabCells3D[a]=slabCount3D(c,a);
            allocateFloats3D(g.cpml3D[a],g.slabCells3D[a]*6,g.stream3D);
            const auto coefficients=makeCoefficients3D(c,a);
            allocateFloats3D(g.coefficients3D[a],coefficients.size(),g.stream3D);
            checkCuda3D(cudaMemcpyAsync(g.coefficients3D[a],coefficients.data(),coefficients.size()*sizeof(float),
                cudaMemcpyHostToDevice,g.stream3D),"upload CPML coefficients");
            checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish coefficient upload");
        }
        checkCuda3D(cudaMalloc(&g.viewStatistics3D,5*sizeof(unsigned int)),"allocate statistics");
        checkCuda3D(cudaMemsetAsync(g.viewStatistics3D,0,5*sizeof(unsigned int),g.stream3D),"clear statistics");
        generateMaterialsKernel3D<<<unsigned((c.cells3D+255)/256),256,0,g.stream3D>>>(g,c); checkLaunch3D();
        checkCuda3D(cudaStreamSynchronize(g.stream3D),"initialize model");
        g.allocatedBytes3D=budget.fieldBytes3D+budget.cpmlBytes3D;
    } catch (...) { releaseGpu3D(g); throw; }
}

// Release an entire independent CUDA lifetime without resetting the device or touching 2-D state.
void releaseGpu3D(GPUSimData3D& g) noexcept {
    if (g.stream3D) cudaStreamSynchronize(g.stream3D);
    for (auto p:g.field3D) cudaFree(p);
    cudaFree(g.inverseDensity3D); cudaFree(g.lambda3D); cudaFree(g.mu3D);
    for (int a=0;a<3;++a) { cudaFree(g.cpml3D[a]); cudaFree(g.coefficients3D[a]); }
    cudaFree(g.receivers3D); cudaFree(g.receiverValues3D); cudaFree(g.viewStatistics3D);
    if (g.hostReceiverValues3D) cudaFreeHost(g.hostReceiverValues3D);
    if (g.startEvent3D) cudaEventDestroy(g.startEvent3D);
    if (g.stopEvent3D) cudaEventDestroy(g.stopEvent3D);
    if (g.stream3D) cudaStreamDestroy(g.stream3D);
    g=GPUSimData3D{};
}

// Clear fields and memory variables, leaving the model and observation geometry intact.
void resetGpu3D(GPUSimData3D& g, const SimulationContext3D& c) {
    for (auto p:g.field3D) checkCuda3D(cudaMemsetAsync(p,0,c.cells3D*sizeof(float),g.stream3D),"reset wavefield");
    for (int a=0;a<3;++a) if(g.slabCells3D[a])
        checkCuda3D(cudaMemsetAsync(g.cpml3D[a],0,g.slabCells3D[a]*6*sizeof(float),g.stream3D),"reset CPML");
    checkCuda3D(cudaMemsetAsync(g.viewStatistics3D,0,5*sizeof(unsigned int),g.stream3D),"reset diagnostics");
}

// Prepare all profiles before committing new frequency-dependent coefficients to the existing stream.
void refreshCpmlCoefficients3D(GPUSimData3D& g,const SimulationContext3D& c) {
    if(!g.stream3D) throw std::logic_error("Seis3D: simulation not initialized.");
    if(c.reflecting3D) return;
    std::array<std::vector<float>,3> profiles;
    for(int axis=0;axis<3;++axis) profiles[axis]=makeCoefficients3D(c,axis);
    float* replacement[3]{};
    try {
        for(int axis=0;axis<3;++axis) {
            allocateFloats3D(replacement[axis],profiles[axis].size(),g.stream3D);
            checkCuda3D(cudaMemcpyAsync(replacement[axis],profiles[axis].data(),profiles[axis].size()*sizeof(float),
                cudaMemcpyHostToDevice,g.stream3D),"refresh CPML coefficients");
        }
        checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish CPML refresh");
    } catch(...) {
        cudaStreamSynchronize(g.stream3D);
        for(auto pointer:replacement) cudaFree(pointer);
        throw;
    }
    for(int axis=0;axis<3;++axis) {std::swap(g.coefficients3D[axis],replacement[axis]);cudaFree(replacement[axis]);}
}

// Enqueue second-order leapfrog updates and stagger-aware planar free-surface ghost maps.
void runStep3D(GPUSimData3D& g, const SimulationContext3D& c, const Source3D& source, std::uint64_t step) {
    if (!g.stream3D) throw std::logic_error("Seis3D: simulation not initialized.");
    if (source.x3D<1 || source.x3D>=c.nx3D-1 || source.y3D<1 || source.y3D>=c.ny3D-1
        || source.z3D<2 || source.z3D>=c.nz3D-1 || !std::isfinite(source.amplitude3D))
        throw std::invalid_argument("Seis3D: source must be inside the physical volume, at depth >=2.");
    for (float d:source.direction3D) if (!std::isfinite(d)) throw std::invalid_argument("Seis3D: invalid force direction.");
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
    checkCuda3D(cudaMemsetAsync(g.viewStatistics3D,0,sizeof(unsigned int),g.stream3D),"reset view maximum");
    checkCuda3D(cudaMemsetAsync(g.viewStatistics3D+4,0,sizeof(unsigned int),g.stream3D),"reset common velocity maximum");
    const unsigned int rangeInitial[2]={0x7f7fffffu,0u};
    checkCuda3D(cudaMemcpyAsync(g.viewStatistics3D+2,rangeInitial,sizeof(rangeInitial),cudaMemcpyHostToDevice,g.stream3D),"reset material range");
    const auto count=volume ? layout.totalSamples3D : layout.volumeOffset3D;
    extractKernel3D<<<unsigned((count+255)/256),256,0,g.stream3D>>>(g,c,out,layout,slices[0],slices[1],slices[2],component,materialComponent,volume,includeBoundary);
    checkLaunch3D();
    unsigned int stats[5]{};
    checkCuda3D(cudaMemcpyAsync(stats,g.viewStatistics3D,sizeof(stats),cudaMemcpyDeviceToHost,g.stream3D),"read diagnostics");
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish view extraction");
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
            checkCuda3D(cudaMalloc(&positions,std::size_t(count)*sizeof(Receiver3D)),"allocate receivers");
            checkCuda3D(cudaMalloc(&values,std::size_t(count)*3*sizeof(float)),"allocate receiver values");
            checkCuda3D(cudaMallocHost(&host,std::size_t(count)*3*sizeof(float)),"allocate pinned receivers");
            checkCuda3D(cudaMemcpyAsync(positions,receivers,std::size_t(count)*sizeof(Receiver3D),cudaMemcpyHostToDevice,g.stream3D),"upload receivers");
        }
        checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish receiver update");
    } catch (...) { cudaFree(positions); cudaFree(values); if (host) cudaFreeHost(host); throw; }
    cudaFree(g.receivers3D); cudaFree(g.receiverValues3D); if (g.hostReceiverValues3D) cudaFreeHost(g.hostReceiverValues3D);
    g.receivers3D=positions; g.receiverValues3D=values; g.hostReceiverValues3D=host; g.receiverCount3D=count;
}

// Gather only receiver values, preserving full wavefields on the GPU.
const float* sampleReceivers3D(GPUSimData3D& g, const SimulationContext3D& c) {
    if (!g.receiverCount3D) return nullptr;
    receiverKernel3D<<<(g.receiverCount3D+255)/256,256,0,g.stream3D>>>(g,c); checkLaunch3D();
    checkCuda3D(cudaMemcpyAsync(g.hostReceiverValues3D,g.receiverValues3D,std::size_t(g.receiverCount3D)*3*sizeof(float),
        cudaMemcpyDeviceToHost,g.stream3D),"gather receivers");
    unsigned int invalid=0;
    checkCuda3D(cudaMemcpyAsync(&invalid,g.viewStatistics3D+1,sizeof(invalid),cudaMemcpyDeviceToHost,g.stream3D),"check capture stability");
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish receiver gather");
    if (invalid) throw std::runtime_error("Seis3D: non-finite wavefield detected; capture stopped before export.");
    return g.hostReceiverValues3D;
}

// Query just one material node, using the sandbox stream rather than device-wide synchronization.
void probeMaterial3D(GPUSimData3D& g, const SimulationContext3D& c, const Receiver3D& p, float values[3]) {
    if (p.x3D<0 || p.x3D>=c.nx3D || p.y3D<0 || p.y3D>=c.ny3D || p.z3D<0 || p.z3D>=c.nz3D) return;
    const auto i=gridIndex3D(c,p.x3D+c.ox3D,p.y3D+c.oy3D,p.z3D+c.oz3D);
    float inverse=0,mu=0,lambda=0;
    checkCuda3D(cudaMemcpyAsync(&inverse,g.inverseDensity3D+i,4,cudaMemcpyDeviceToHost,g.stream3D),"probe density");
    checkCuda3D(cudaMemcpyAsync(&mu,g.mu3D+i,4,cudaMemcpyDeviceToHost,g.stream3D),"probe mu");
    checkCuda3D(cudaMemcpyAsync(&lambda,g.lambda3D+i,4,cudaMemcpyDeviceToHost,g.stream3D),"probe lambda");
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish material probe");
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
    for (int a=0;a<3;++a) checkCuda3D(cudaMemcpy2DAsync(destinations[a]+offset,std::size_t(c.gx3D)*4,
        material.data()+a*n,std::size_t(c.nx3D)*4,std::size_t(c.nx3D)*4,c.ny3D,cudaMemcpyHostToDevice,g.stream3D),"upload material plane");
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish material plane");
}

// Extend physical edge cells to all padding before the first simulation step.
void finishModelUpload3D(GPUSimData3D& g, const SimulationContext3D& c) {
    extendMaterialsKernel3D<<<unsigned((c.cells3D+255)/256),256,0,g.stream3D>>>(g,c); checkLaunch3D();
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish model import");
}

// Download a material plane with bounded CPU memory and convert Lamé values back to SI materials.
void downloadModelPlane3D(GPUSimData3D& g, const SimulationContext3D& c, int z, float* values) {
    if (z<0 || z>=c.nz3D || !values) throw std::invalid_argument("Seis3D: invalid export plane.");
    const std::size_t n=std::size_t(c.nx3D)*c.ny3D;
    std::vector<float> material(n*3);
    const auto offset=gridIndex3D(c,c.ox3D,c.oy3D,z+c.oz3D);
    const float* sources[3]={g.inverseDensity3D,g.lambda3D,g.mu3D};
    for (int a=0;a<3;++a) checkCuda3D(cudaMemcpy2DAsync(material.data()+a*n,std::size_t(c.nx3D)*4,
        sources[a]+offset,std::size_t(c.gx3D)*4,std::size_t(c.nx3D)*4,c.ny3D,cudaMemcpyDeviceToHost,g.stream3D),"download material plane");
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish model plane export");
    for (std::size_t i=0;i<n;++i) {
        values[3*i]=std::sqrt((material[n+i]+2*material[2*n+i])*material[i]);
        values[3*i+1]=std::sqrt(material[2*n+i]*material[i]); values[3*i+2]=1/material[i];
    }
}

// Download only the field planes needed for the viewer's staggered-grid collocation.
void downloadWavefieldPlane3D(GPUSimData3D& g,const SimulationContext3D& c,int z,int component,float* values,float* scratch) {
    if(!g.stream3D||z<0||z>=c.nz3D||component<0||component>4||!values||!scratch)
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
        checkCuda3D(cudaMemcpy2DAsync(scratch+std::size_t(sourceIndex3D)*count,hostPitch,
            g.field3D[source3D.field3D]+offset3D,devicePitch,hostPitch,c.ny3D,cudaMemcpyDeviceToHost,g.stream3D),
            "download wavefield plane");
    }
    checkCuda3D(cudaStreamSynchronize(g.stream3D),"finish wavefield plane export");
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
