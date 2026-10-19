#pragma once
#include "Common3D.h"
#include <cmath>
#include <cstring>
#ifdef __CUDACC__
#define SEIS3_CELL __host__ __device__ inline
#else
#define SEIS3_CELL inline
#endif
namespace Seis3D::Cells3D {
SEIS3_CELL bool finiteValue3D(float value) {
#ifdef __CUDA_ARCH__
 return ::isfinite(value);
#else
 return std::isfinite(value);
#endif
}
template<class T> SEIS3_CELL T min(T a,T b) {return a<b?a:b;}
template<class T> SEIS3_CELL T max(T a,T b) {return a>b?a:b;}
SEIS3_CELL unsigned floatBits3D(float value) {union {float f;unsigned u;} bits;bits.f=value;return bits.u;}
SEIS3_CELL void statsExchange(unsigned* p,unsigned value) {
#ifdef __CUDA_ARCH__
 atomicExch(p,value);
#else
 #pragma omp critical(Seis3Statistics)
 {*p=value;}
#endif
}
SEIS3_CELL void statsMax(unsigned* p,unsigned value) {
#ifdef __CUDA_ARCH__
 atomicMax(p,value);
#else
 *p=max(*p,value);
#endif
}
SEIS3_CELL void statsMin(unsigned* p,unsigned value) {
#ifdef __CUDA_ARCH__
 atomicMin(p,value);
#else
 *p=min(*p,value);
#endif
}
// Flatten an X-contiguous padded node on the device.
SEIS3_CELL std::size_t index3D(const SimulationContext3D& c, int x, int y, int z) {
    return (std::size_t(z) * c.gy3D + y) * c.gx3D + x;
}

// Return eighth-order staggered first-derivative weights, shared by all three axes.
SEIS3_CELL float weight3D(int r) {
    return r == 0 ? 1225.0f / 1024.0f : r == 1 ? -245.0f / 3072.0f
        : r == 2 ? 49.0f / 5120.0f : -5.0f / 7168.0f;
}

// Evaluate D+ at i+1/2 or D- at i, using eight samples along the selected axis.
template<bool plus>
SEIS3_CELL float derivative3D(const float* field, std::size_t i,
    std::size_t stride, float inverseSpacing) {
    float result = 0;
    #ifdef __CUDACC__
    #pragma unroll
    #endif
    for (int r = 0; r < 4; ++r) {
        const std::size_t positive = i + std::size_t(r + (plus ? 1 : 0)) * stride;
        const std::size_t negative = i - std::size_t(r + (plus ? 0 : 1)) * stride;
        result += weight3D(r) * (field[positive] - field[negative]);
    }
    return result * inverseSpacing;
}

// Apply one derivative's CPML recurrence in its direction's compact slab allocation.
SEIS3_CELL float applyCpml3D(const GPUSimData3D& g, const SimulationContext3D& c,
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
SEIS3_CELL void generateMaterialsKernel3D(GPUSimData3D g, SimulationContext3D c,std::size_t i) {
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
SEIS3_CELL void reflectingGhostKernel3D(GPUSimData3D g, SimulationContext3D c, int axis, bool velocities,int u,int v) {
    const int lengths[3] = {c.gx3D,c.gy3D,c.gz3D};
    const int uAxis = (axis+1)%3, vAxis = (axis+2)%3;
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


// Extend an imported physical domain into CPML and ghost cells without changing physical cells.
SEIS3_CELL void extendMaterialsKernel3D(GPUSimData3D g, SimulationContext3D c,std::size_t i) {
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
SEIS3_CELL void stressGhostKernel3D(GPUSimData3D g, SimulationContext3D c,int x,int y) {
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
SEIS3_CELL void velocityKernel3D(GPUSimData3D g, SimulationContext3D c,int x,int y,int z) {
    if (x >= c.gx3D - 4 || y >= c.gy3D - 4 || z >= c.gz3D - 4) return;
    const auto i = index3D(c, x, y, z);
    const std::size_t strides[3] = { 1, std::size_t(c.gx3D), std::size_t(c.gx3D) * c.gy3D };
    const int stressFields[3][3] = { {3,6,8}, {6,4,7}, {8,7,5} };
    #ifdef __CUDACC__
    #pragma unroll
    #endif
    for (int v = 0; v < 3; ++v) {
        float divergence = 0;
        #ifdef __CUDACC__
    #pragma unroll
    #endif
        for (int axis = 0; axis < 3; ++axis) {
            const float d = axis == v
                ? derivative3D<true>(g.field3D[stressFields[v][axis]], i, strides[axis], 1.0f / c.spacing3D)
                : derivative3D<false>(g.field3D[stressFields[v][axis]], i, strides[axis], 1.0f / c.spacing3D);
            divergence += applyCpml3D(g, c, x, y, z, axis, v, axis == v, d);
        }
        const float a = g.inverseDensity3D[i], b = g.inverseDensity3D[i + strides[v]];
        const float value = g.field3D[v][i] + c.dt3D * (2 * a * b / (a + b)) * divergence;
        g.field3D[v][i] = value;
        if (!finiteValue3D(value)) statsExchange(g.viewStatistics3D + 1, 1u);
    }
}


// Map velocity ghosts using the three zero-traction differential constraints at z=0.
// Interior differences remain eighth order; this planar surface closure is second order.
SEIS3_CELL void velocityGhostKernel3D(GPUSimData3D g, SimulationContext3D c,int x,int y) {
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
SEIS3_CELL float shearMu3D(const float* mu, std::size_t i, std::size_t a, std::size_t b) {
    const float m0 = mu[i], m1 = mu[i+a], m2 = mu[i+b], m3 = mu[i+a+b];
    return (m0 > 0 && m1 > 0 && m2 > 0 && m3 > 0) ? 4 / (1/m0 + 1/m1 + 1/m2 + 1/m3) : 0;
}

// Update the six stresses using independently filtered derivatives and an unsplit constitutive law.
SEIS3_CELL void stressKernel3D(GPUSimData3D g, SimulationContext3D c,int x,int y,int z) {
    if (x >= c.gx3D - 4 || y >= c.gy3D - 4 || z >= c.gz3D - 4) return;
    const auto i = index3D(c, x, y, z);
    const std::size_t stride[3] = { 1, std::size_t(c.gx3D), std::size_t(c.gx3D) * c.gy3D };
    float d[3][3]; // d[velocity component][derivative axis].
    #ifdef __CUDACC__
    #pragma unroll
    #endif
    for (int v = 0; v < 3; ++v) {
        #ifdef __CUDACC__
    #pragma unroll
    #endif
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
    #ifdef __CUDACC__
    #pragma unroll
    #endif
    for (int a = 3; a < 9; ++a) if (!finiteValue3D(g.field3D[a][i])) statsExchange(g.viewStatistics3D + 1, 1u);
}


// Inject a normalized 3x3x3 Ricker source at the correct velocity/stress leapfrog time.
SEIS3_CELL void sourceKernel3D(GPUSimData3D g, SimulationContext3D c, Source3D source, double time,int a) {
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
SEIS3_CELL float sampleScalar3D(const GPUSimData3D& g, const SimulationContext3D& c,
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
SEIS3_CELL float resampleScalar3D(const GPUSimData3D& g,const SimulationContext3D& c,
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
SEIS3_CELL void extractKernel3D(GPUSimData3D g, SimulationContext3D c, float* output,
    ViewLayout3D layout, int sx, int sy, int sz, int component, int materialComponent, bool volume, bool includeBoundary,std::size_t i) {
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
    output[2*i] = finiteValue3D(value) ? value : 0;
    output[2*i+1] = finiteValue3D(material) ? material : 0;
    if (!finiteValue3D(value) || !finiteValue3D(material) || !finiteValue3D(magnitude) || material < 0) statsExchange(g.viewStatistics3D+1, 1u);
    // Reduce within each active warp before atomics, including the final partially populated warp.
    const bool physical=x>=0&&x<c.nx3D&&y>=0&&y<c.ny3D&&z>=0&&z<c.nz3D;
    float waveMax=(physical||includeBoundary)&&finiteValue3D(value)?fabsf(value):0;
    float velocityMax=(physical||includeBoundary)&&finiteValue3D(magnitude)?magnitude:0;
    float materialMin=physical&&finiteValue3D(material)?material:3.402823466e38f;
    float materialMax=physical&&finiteValue3D(material)?material:0;
#ifdef __CUDA_ARCH__
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
        statsMax(g.viewStatistics3D,__float_as_uint(waveMax));
        statsMin(g.viewStatistics3D+2,__float_as_uint(materialMin));
        statsMax(g.viewStatistics3D+3,__float_as_uint(materialMax));
        statsMax(g.viewStatistics3D+4,__float_as_uint(velocityMax));
    }
#else
        statsMax(g.viewStatistics3D,floatBits3D(waveMax));
        statsMin(g.viewStatistics3D+2,floatBits3D(materialMin));
        statsMax(g.viewStatistics3D+3,floatBits3D(materialMax));
        statsMax(g.viewStatistics3D+4,floatBits3D(velocityMax));
#endif

}


// Gather receiver velocities into a compact interleaved buffer, validating coordinates on upload.
SEIS3_CELL void receiverKernel3D(GPUSimData3D g, SimulationContext3D c,int r) {
    if (r >= g.receiverCount3D) return;
    const auto p = g.receivers3D[r];
    g.receiverValues3D[3*r] = sampleScalar3D(g,c,p.x3D,p.y3D,p.z3D,1);
    g.receiverValues3D[3*r+1] = sampleScalar3D(g,c,p.x3D,p.y3D,p.z3D,2);
    g.receiverValues3D[3*r+2] = sampleScalar3D(g,c,p.x3D,p.y3D,p.z3D,0);
}



}
#undef SEIS3_CELL
