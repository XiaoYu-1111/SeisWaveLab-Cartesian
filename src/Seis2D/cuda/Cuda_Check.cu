#include <cooperative_groups/reduce.h>
#include "ExternalDomain2D.h"
#include <iostream>
#include <string.h>
#include <algorithm> // 确保 std::max 可用

#include "Cuda_Check.cuh"
#include "SolverCells2D.h"
#include "CpuSolver2D.h"
#include <device_launch_parameters.h>

#include <thrust/reduce.h>
#include <thrust/device_ptr.h>
#include <thrust/execution_policy.h>

#include <cuda_runtime.h>
#include <curand_kernel.h>

// 防御性定义 M_PI，防止 MSVC 环境下 math 常量未导出的编译错误
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// =============================================================================
// 1. 基础硬件测试模块
// =============================================================================

// 加法核函数
// Adds vector elements for the startup CUDA smoke test.
__global__ void vectorAddKernel(const float* a, const float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}

// 辅助函数：根据架构计算核心数
// Maps a compute-capability version to the estimated CUDA cores per multiprocessor.
int convertSmVersionToCudaCoreCount(int major, int minor) {
    switch (major) {
    case 7:  return 64; // Volta, Turing (7.0, 7.5)
    case 8:  return (minor == 0) ? 64 : 128; // Ampere (8.0=64, 8.6/8.9=128)
    case 9:  return 128; // Hopper
    default: return 128;
    }
}

// 获取 GPU 设备信息
// Returns the properties of the active CUDA device for startup checks and diagnostics.
extern "C" GpuInfo getCudaDeviceInfo() {
    GpuInfo info;
    memset(&info, 0, sizeof(info));
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0) return info;

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);

    // 使用具有跨平台移植性的 snprintf
    snprintf(info.name, sizeof(info.name), "%s", prop.name);
    info.computeCapabilityMajor = prop.major;
    info.computeCapabilityMinor = prop.minor;
    info.totalMem = prop.totalGlobalMem;
    info.multiProcessorCount = prop.multiProcessorCount;
    info.clockRate = prop.clockRate;

    // 计算核心数：SMs * CoresPerSM
    info.cudaCores = prop.multiProcessorCount * convertSmVersionToCudaCoreCount(prop.major, prop.minor);
    info.success = true;
    return info;
}

// Verifies a minimal host-to-device, kernel, and device-to-host execution path.
extern "C" bool runCudaTest(float* hostA, float* hostB, float* hostC, int elementCount) {
    float* d_a = nullptr, * d_b = nullptr, * d_c = nullptr;
    size_t bytes = elementCount * sizeof(float);

    if (cudaMalloc(&d_a, bytes) != cudaSuccess) return false;
    if (cudaMalloc(&d_b, bytes) != cudaSuccess) { cudaFree(d_a); return false; }
    if (cudaMalloc(&d_c, bytes) != cudaSuccess) { cudaFree(d_a); cudaFree(d_b); return false; }

    cudaMemcpy(d_a, hostA, bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(d_b, hostB, bytes, cudaMemcpyHostToDevice);

    int threads = 256;
    int blocks = (elementCount + threads - 1) / threads;
    vectorAddKernel << <blocks, threads >> > (d_a, d_b, d_c, elementCount);

    if (cudaDeviceSynchronize() != cudaSuccess) {
        cudaFree(d_a); cudaFree(d_b); cudaFree(d_c);
        return false;
    }

    cudaMemcpy(hostC, d_c, bytes, cudaMemcpyDeviceToHost);

    cudaFree(d_a);
    cudaFree(d_b);
    cudaFree(d_c);
    return true;
}

// =============================================================================
// 2. 生命游戏模块
// =============================================================================

static curandState* d_states = nullptr; // 随机数状态缓冲区

// 释放可选演示模块的随机状态，允许退出清理重复调用。
extern "C" void releaseCudaLife() {
    cudaFree(d_states);
    d_states = nullptr;
}

// Initializes one CURAND state per cell for the life-game demonstration.
__global__ void initializeRandomStatesKernel(curandState* states, unsigned long seed, int w, int h) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < w * h) {
        curand_init(seed, idx, 0, &states[idx]);
    }
}

// Allocates and seeds the random states used by the standalone life-game demo.
extern "C" void initializeCudaLife(int w, int h) {
    if (d_states) cudaFree(d_states);
    cudaMalloc(&d_states, w * h * sizeof(curandState));

    int threads = 256;
    int blocks = (w * h + threads - 1) / threads;
    initializeRandomStatesKernel << <blocks, threads >> > (d_states, (unsigned long)time(0), w, h);
    cudaDeviceSynchronize();
}

// Writes a random binary state into each life-game cell.
__global__ void seedLifeGridKernel(uint8_t* world, curandState* states, float density, int w, int h) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < w * h) {
        float r = curand_uniform(&states[idx]);
        world[idx] = (r < density) ? 1 : 0;
    }
}

// Initializes every cell using the requested live-cell density.
extern "C" void seedCudaLife(uint8_t* d_world, int w, int h, float density) {
    int n = w * h;
    seedLifeGridKernel << <(n + 255) / 256, 256 >> > (d_world, d_states, density, w, h);
    cudaDeviceSynchronize();
}

// Applies the configured birth/survival rules and decays the per-cell heat map.
__global__ void updateLifeGridKernel(const uint8_t* current, uint8_t* next, float* heatMap, int w, int h, float dt,
    bool paused, float decay, int b_mask, int s_mask) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h) return;
    int idx = y * w + x;

    if (!paused) {
        int neighbors = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                neighbors += current[((y + dy + h) % h) * w + ((x + dx + w) % w)];
            }
        }
        uint8_t alive = current[idx];
        if (alive) {
            next[idx] = (s_mask & (1 << neighbors)) ? 1 : 0;
        }
        else {
            next[idx] = (b_mask & (1 << neighbors)) ? 1 : 0;
        }
    }

    float hVal = heatMap[idx];
    if (next[idx] == 1) {
        hVal = 1.0f;
    }
    else {
        hVal *= decay;
        if (hVal < 0.005f) hVal = 0.0f;
    }
    heatMap[idx] = hVal;
}

// Advances one life-game frame and updates the heat-map buffer.
extern "C" void updateCudaLife(uint8_t* d_current, uint8_t* d_next, float* d_heatMap, int w, int h, float deltaTime,
    bool paused, float decay, int b_mask, int s_mask) {
    dim3 block(16, 16);
    dim3 grid((w + 15) / 16, (h + 15) / 16);
    updateLifeGridKernel << <grid, block >> > (d_current, d_next, d_heatMap, w, h, deltaTime, paused, decay, b_mask, s_mask);
}

// Applies a circular brush to cells within the image bounds.
__global__ void paintLifeGridKernel(uint8_t* world, float* heatMap, int w, int h, int mx, int my, int radius, bool erase) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= w || y >= h) return;

    float dist = sqrtf(powf(x - mx, 2) + powf(y - my, 2));
    if (dist < radius) {
        int idx = y * w + x;
        world[idx] = erase ? 0 : 1;
        if (!erase) heatMap[idx] = 1.0f;
    }
}

// Paints or erases cells within the requested circular brush on the GPU.
extern "C" void paintCudaLife(uint8_t* d_world, float* d_heat, int w, int h, int mx, int my, int radius, bool erase) {
    dim3 block(16, 16);
    dim3 grid((w + 15) / 16, (h + 15) / 16);
    paintLifeGridKernel << <grid, block >> > (d_world, d_heat, w, h, mx, my, radius, erase);
}

// Returns the number of live cells in the current life-game grid.
extern "C" int getCudaLifePopulation(uint8_t* d_world, int w, int h) {
    thrust::device_ptr<uint8_t> ptr(d_world);
    try {
        return thrust::reduce(thrust::device, ptr, ptr + (w * h), (int)0, thrust::plus<int>());
    }
    catch (...) {
        return 0;
    }
}

// =============================================================================
// 3. 地震数值模拟核心核函数 (波场演化)
// =============================================================================

// --- Type 1 PML Stress Update ---
// Updates split stress fields using the full-domain split-field PML scheme.
__global__ void updateType1StressKernel(GPUSimData g) {Seis2DCells::updateType1Stress(g,blockIdx.y*blockDim.y+threadIdx.y,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Type 1 PML Velocity Update ---
// Updates split particle-velocity fields for the full-domain PML scheme.
__global__ void updateType1VelocityKernel(GPUSimData g) {Seis2DCells::updateType1Velocity(g,blockIdx.y*blockDim.y+threadIdx.y,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Type 2 PML/FDM Hybrid Stress Update ---
// Updates stress fields for the interior-FDM and boundary-PML hybrid scheme.
__global__ void updateType2StressKernel(GPUSimData g) {Seis2DCells::updateType2Stress(g,blockIdx.y*blockDim.y+threadIdx.y,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Type 2 PML/FDM Hybrid Velocity Update ---
// Updates velocity fields for the interior-FDM and boundary-PML hybrid scheme.
__global__ void updateType2VelocityKernel(GPUSimData g) {Seis2DCells::updateType2Velocity(g,blockIdx.y*blockDim.y+threadIdx.y,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Type 3 Free Surface Stress Update ---
// Updates split stress fields while enforcing the traction-free surface condition.
__global__ void updateType3StressKernel(GPUSimData g) {Seis2DCells::updateType3Stress(g,blockIdx.y*blockDim.y+threadIdx.y,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Type 3 Stress Mirroring ---
// Mirrors stress values across the free surface to provide antisymmetric stencil samples.
__global__ void mirrorType3StressKernel(GPUSimData g) {Seis2DCells::mirrorType3Stress(g,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Type 3 Free Surface Velocity Update ---
// Updates velocity fields for the free-surface solver and clears the air region.
__global__ void updateType3VelocityKernel(GPUSimData g) {Seis2DCells::updateType3Velocity(g,blockIdx.y*blockDim.y+threadIdx.y,blockIdx.x*blockDim.x+threadIdx.x);}

// --- Source Injection ---
// Injects one time-sampled source into the selected stress or velocity field.
using Seis2DCells::applyPointSource;

// --- Zero Dirichlet Boundary ---
// 单点和多点震源共用同一种场注入方式，避免震源类型在两条路径中不一致。
__global__ void injectSourceKernel(GPUSimData g, float waveletValue, int sourceIndex) {
    applyPointSource(g, waveletValue, sourceIndex);
}

// Clears the outer stencil guard cells to prevent invalid edge reads.
__global__ void zeroBoundaryKernel(GPUSimData g) {Seis2DCells::zeroBoundary(g,blockIdx.x*blockDim.x+threadIdx.x);}
// =============================================================================
// 【新增】：设备端安全越界钳位速度提取函数 (用于高阶有限差分求导)
// =============================================================================
// =============================================================================
// 【安全修复】：使用三目运算符彻底规避 windows.h 宏冲突与平台编译问题
// =============================================================================
using Seis2DCells::get_vx;
using Seis2DCells::get_vz;
// =============================================================================
// 【重构】：7 分量多重波场自适应实时色彩映射核函数 [2]
// =============================================================================
// =============================================================================
// 【自适应物理平衡】：7 分量多重波场自适应实时色彩映射核函数
// =============================================================================
// Converts a selected physical field component into an RGBA visualization buffer.
__global__ void generateColorMapKernel(GPUSimData g, uchar4* rgba_out, float scale, int component,float4* normalized_out,unsigned int* peak) {
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    int i = blockIdx.y * blockDim.y + threadIdx.y;

    if (i < g.NZ && j < g.NX) {
        int k = i * g.NX + j;
        float component_balancer=1;
        float val=Seis2DCells::component(g,i,j,component,component_balancer);
        if(peak) {
            bool physical=g.physicalNX<=0||(j>=g.originX&&j<g.originX+g.physicalNX&&i>=g.originZ&&i<g.originZ+g.physicalNZ);
            float amplitude=fabsf(val*component_balancer);
            auto group=cooperative_groups::coalesced_threads();
            const float maximum=cooperative_groups::reduce(group,physical&&isfinite(amplitude)?amplitude:0.0f,cooperative_groups::greater<float>());
            if(group.thread_rank()==0) atomicMax(peak,__float_as_uint(maximum));
            if(!normalized_out&&!rgba_out) return;
        }
        // 应用自适应物理平衡因子，使所有通道在同一个 Color Gain 滑块下拥有高度一致的视觉密度
        float norm = val * scale * component_balancer;
        if (norm > 1.0f)  norm = 1.0f;
        if (norm < -1.0f) norm = -1.0f;

        if(normalized_out) {normalized_out[k]=make_float4(val,norm,component_balancer,1);return;}
        unsigned char r = 255, g_val = 255, b = 255;
        if (norm > 0.0f) { // 正振幅 -> 红色
            g_val = static_cast<unsigned char>(255.0f * (1.0f - norm));
            b = static_cast<unsigned char>(255.0f * (1.0f - norm));
        }
        else { // 负振幅 -> 蓝色
            r = static_cast<unsigned char>(255.0f * (1.0f + norm));
            g_val = static_cast<unsigned char>(255.0f * (1.0f + norm));
        }

        rgba_out[k] = make_uchar4(r, g_val, b, 255);
    }
}

// Gathers Vx and Vz from flattened receiver indices for one simulation time step.
__global__ void extractReceiverSamplesKernel(
    const float* __restrict__ vx_1, const float* __restrict__ vx_2,
    const float* __restrict__ vz_1, const float* __restrict__ vz_2,
    const float* __restrict__ vx, const float* __restrict__ vz,
    const int* __restrict__ rcv_grid_idx,
    float* d_record_vx_step, float* d_record_vz_step,
    int num_rcv, int flag_type, int gridElementCount
) {
    int r = blockIdx.x * blockDim.x + threadIdx.x;
    if (r < num_rcv) {
        int k = rcv_grid_idx[r];
        if (k < 0 || k >= gridElementCount) {
            d_record_vx_step[r] = 0.0f;
            d_record_vz_step[r] = 0.0f;
            return;
        }
        if (flag_type == 1 || flag_type == 3) {
            d_record_vx_step[r] = vx_1[k] + vx_2[k];
            d_record_vz_step[r] = vz_1[k] + vz_2[k];
        }
        else {
            d_record_vx_step[r] = vx[k];
            d_record_vz_step[r] = vz[k];
        }
    }
}

// --- Multi-source Injection Kernel ---
// Injects all active point sources from the device-side source array.
__global__ void injectMultipleSourcesKernel(GPUSimData g, const GPUSource* d_sources, int num_sources) {
    int r = threadIdx.x;
    if (r < num_sources) {
        GPUSource src = d_sources[r];
        if (!isfinite(src.f_peak) || src.f_peak <= 0.0f || !isfinite(src.t)) return;

        float t_peak = 1.0f / src.f_peak;
        float x = M_PI * src.f_peak * (src.t - t_peak);
        float val = src.amp * (1.0f - 2.0f * x * x) * expf(-x * x);

        applyPointSource(g, val * kSourceInjectionScale2D, src.idx);
    }
}

// =============================================================================
// 主机驱动接口实现 (Host Drivers)
// =============================================================================

// 在任何 CUDA 分配前拒绝非法尺寸、数组长度和非物理材料参数。
void validateSimulationContext(const SimulationContext& ctx) {
    const size_t elementCount = checkedGridElementCount(ctx.NX, ctx.NZ);
    validateBoundaryGeometry(ctx.NX, ctx.NZ, ctx.npml, ctx.flag_type == 3 && ctx.upFlag);
    if (ctx.flag_type < 1 || ctx.flag_type > 3 || ctx.total_grid != elementCount ||
        !std::isfinite(ctx.dt) || ctx.dt <= 0.0f || !std::isfinite(ctx.h) || ctx.h <= 0.0f || ctx.nt <= 0) {
        throw std::invalid_argument("Invalid solver, time grid, or spatial grid");
    }
    if (ctx.rho.size() != elementCount || ctx.mu.size() != elementCount ||
        ctx.lambda.size() != elementCount || ctx.lambda2mu.size() != elementCount ||
        ctx.dx.size() != ctx.NX || ctx.dx_half.size() != ctx.NX ||
        ctx.dz.size() != ctx.NZ || ctx.dz_half.size() != ctx.NZ ||
        (ctx.flag_type == 3 && ctx.dp_flat.size() != ctx.NX) ||
        ctx.wavelet.size() < static_cast<size_t>(ctx.nt) || ctx.num_rcv < 0 ||
        ctx.rcv_grid_idx.size() != static_cast<size_t>(ctx.num_rcv)) {
        throw std::invalid_argument("Simulation array lengths do not match the grid or receiver count");
    }
    float maximumVp = 0.0f;
    for (size_t k = 0; k < elementCount; ++k) {
        if (!std::isfinite(ctx.rho[k]) || ctx.rho[k] <= 0.0f ||
            !std::isfinite(ctx.mu[k]) || ctx.mu[k] < 0.0f ||
            !std::isfinite(ctx.lambda[k]) || !std::isfinite(ctx.lambda2mu[k]) || ctx.lambda2mu[k] <= 0.0f) {
            throw std::invalid_argument("Density/stiffness must be finite with rho > 0, mu >= 0, lambda2mu > 0");
        }
        maximumVp = std::max(maximumVp, std::sqrt(ctx.lambda2mu[k] / ctx.rho[k]));
        if (ctx.lambda2mu[k] - (4.0f / 3.0f) * ctx.mu[k] <= 0.0f) {
            throw std::invalid_argument("Elastic bulk modulus must be positive");
        }
    }
    if (!std::isfinite(maximumVp) || ctx.dt * maximumVp / ctx.h > kCourantLimit2d) {
        throw std::invalid_argument("The time step exceeds the 2D staggered-grid CFL limit");
    }
    for (const auto* profile : {&ctx.dx, &ctx.dx_half, &ctx.dz, &ctx.dz_half}) {
        for (float damping : *profile) {
            if (!std::isfinite(damping) || damping < 0.0f) {
                throw std::invalid_argument("PML damping must be finite and nonnegative");
            }
        }
    }
    for (int receiverIndex : ctx.rcv_grid_idx) {
        if (receiverIndex < 0 || receiverIndex >= ctx.total_grid) {
            throw std::invalid_argument("A receiver lies outside the simulation grid");
        }
    }
}

// Allocates device buffers; failure releases every allocation made by this attempt.
static void initializeGpuCore2D(GPUSimData& gpu, const SimulationContext& ctx,bool cpu) {
    validateSimulationContext(ctx);
    releaseGpuSimulation(gpu);
    gpu.cpu=cpu;gpu.workers=std::max(1,std::min(8,cpuWorkerLimit2D()));
    try {
    gpu.NX = ctx.NX; gpu.NZ = ctx.NZ; gpu.total_grid = ctx.total_grid;
    gpu.dt = ctx.dt; gpu.inv_dt = 1.0f / ctx.dt;
    gpu.flag_type = ctx.flag_type; gpu.npml = ctx.npml;
    gpu.fs_idx = ctx.npml; gpu.upFlag = ctx.upFlag;
    gpu.c1_h = ctx.c1_h; gpu.c2_h = ctx.c2_h; gpu.c3_h = ctx.c3_h; gpu.c4_h = ctx.c4_h;

    size_t bytes = ctx.total_grid * sizeof(float);

    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_vx_1, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_vx_1, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_vx_2, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_vx_2, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_vz_1, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_vz_1, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_vz_2, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_vz_2, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sxx_1, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sxx_1, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sxx_2, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sxx_2, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_szz_1, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_szz_1, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_szz_2, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_szz_2, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sxz_1, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sxz_1, 0, bytes));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sxz_2, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sxz_2, 0, bytes));

    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_rho, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_rho, ctx.rho.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_mu, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_mu, ctx.mu.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_lambda, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_lambda, ctx.lambda.data(), bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_lambda2mu, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_lambda2mu, ctx.lambda2mu.data(), bytes, cudaMemcpyHostToDevice));

    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_dx, ctx.NX * sizeof(float))); CUDA_CHECK(solverCopy2D(gpu,gpu.d_dx, ctx.dx.data(), ctx.NX * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_dx_half, ctx.NX * sizeof(float))); CUDA_CHECK(solverCopy2D(gpu,gpu.d_dx_half, ctx.dx_half.data(), ctx.NX * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_dz, ctx.NZ * sizeof(float))); CUDA_CHECK(solverCopy2D(gpu,gpu.d_dz, ctx.dz.data(), ctx.NZ * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_dz_half, ctx.NZ * sizeof(float))); CUDA_CHECK(solverCopy2D(gpu,gpu.d_dz_half, ctx.dz_half.data(), ctx.NZ * sizeof(float), cudaMemcpyHostToDevice));

    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_displayMaximum,sizeof(float)));
    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_active_sources, kMaximumActiveSources * sizeof(GPUSource)));

    if (gpu.flag_type == 2) {
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_vx, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_vx, 0, bytes));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_vz, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_vz, 0, bytes));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sigmaxx, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sigmaxx, 0, bytes));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sigmazz, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sigmazz, 0, bytes));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_sigmaxz, bytes)); CUDA_CHECK(solverZero2D(gpu,gpu.d_sigmaxz, 0, bytes));

        std::vector<float> mu_x(ctx.total_grid, 0.0f), lambda_x(ctx.total_grid, 0.0f), l2m_x(ctx.total_grid, 0.0f);
        std::vector<float> mu_z(ctx.total_grid, 0.0f), rho_xz(ctx.total_grid, 0.0f), rho_orig(ctx.total_grid, 0.0f);
        for (int i = 0; i < ctx.NZ; ++i) {
            for (int j = 0; j < ctx.NX; ++j) {
                int k = i * ctx.NX + j;
                rho_orig[k] = ctx.rho[k];
                int k_next_x = (j < ctx.NX - 1) ? k + 1 : k;
                int k_next_z = (i < ctx.NZ - 1) ? k + ctx.NX : k;
                mu_x[k] = 0.5f * (ctx.mu[k] + ctx.mu[k_next_x]);
                lambda_x[k] = 0.5f * (ctx.lambda[k] + ctx.lambda[k_next_x]);
                l2m_x[k] = 0.5f * (ctx.lambda2mu[k] + ctx.lambda2mu[k_next_x]);
                mu_z[k] = 0.5f * (ctx.mu[k] + ctx.mu[k_next_z]);
                int k_dr = (i < ctx.NZ - 1 && j < ctx.NX - 1) ? k + ctx.NX + 1 : k;
                rho_xz[k] = 0.25f * (ctx.rho[k] + ctx.rho[k_next_x] + ctx.rho[k_next_z] + ctx.rho[k_dr]);
            }
        }
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_mu_x_flat, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_mu_x_flat, mu_x.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_lambda_x_flat, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_lambda_x_flat, lambda_x.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_lambda2mu_x_flat, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_lambda2mu_x_flat, l2m_x.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_mu_z_flat, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_mu_z_flat, mu_z.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_rho_x_z_flat, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_rho_x_z_flat, rho_xz.data(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_rho_orig_flat, bytes)); CUDA_CHECK(solverCopy2D(gpu,gpu.d_rho_orig_flat, rho_orig.data(), bytes, cudaMemcpyHostToDevice));
    }

    if (gpu.flag_type == 3) {
        CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_dp_flat, ctx.NX * sizeof(float)));
        CUDA_CHECK(solverCopy2D(gpu,gpu.d_dp_flat, ctx.dp_flat.data(), ctx.NX * sizeof(float), cudaMemcpyHostToDevice));
    }

    CUDA_CHECK(solverAllocate2D(gpu,&gpu.d_wavelet, ctx.wavelet.size() * sizeof(float)));
    CUDA_CHECK(solverCopy2D(gpu,gpu.d_wavelet, ctx.wavelet.data(), ctx.wavelet.size() * sizeof(float), cudaMemcpyHostToDevice));

    updateGpuReceivers(gpu, ctx.rcv_grid_idx);
    }
    catch (...) {
        releaseGpuSimulation(gpu);
        throw;
    }
}

void initializeGpuSimulation(GPUSimData& gpu,const SimulationContext& physical,bool cpu) {
    if(!physical.externalPml) {initializeGpuCore2D(gpu,physical,cpu);return;}
    auto expanded=extendedContext2D(physical);
    initializeGpuCore2D(gpu,expanded,cpu);
    gpu.physicalNX=physical.NX;gpu.physicalNZ=physical.NZ;
    gpu.originX=gpu.originZ=std::max(physical.npml,kStencilRadius);
}
void uploadActiveSources2D(GPUSimData& gpu,const std::vector<GPUSource>& sources) {
    if(sources.size()>kMaximumActiveSources) throw std::invalid_argument("Active source capacity exceeded");
    auto mapped=sources;
    if(gpu.physicalNX>0) for(auto& source:mapped) {
        if(source.idx<0||source.idx>=gpu.physicalNX*gpu.physicalNZ) throw std::invalid_argument("Source outside physical model");
        source.idx=(source.idx/gpu.physicalNX+gpu.originZ)*gpu.NX+source.idx%gpu.physicalNX+gpu.originX;
    }
    if(!mapped.empty()) CUDA_CHECK(solverCopy2D(gpu,gpu.d_active_sources,mapped.data(),mapped.size()*sizeof(GPUSource),cudaMemcpyHostToDevice));
}

// Releases device buffers owned by the selected solver scheme.
void releaseGpuSimulation(GPUSimData& gpu) {
    solverFree2D(gpu,gpu.d_vx_1); solverFree2D(gpu,gpu.d_vx_2); solverFree2D(gpu,gpu.d_vz_1); solverFree2D(gpu,gpu.d_vz_2);
    solverFree2D(gpu,gpu.d_sxx_1); solverFree2D(gpu,gpu.d_sxx_2); solverFree2D(gpu,gpu.d_szz_1); solverFree2D(gpu,gpu.d_szz_2);
    solverFree2D(gpu,gpu.d_sxz_1); solverFree2D(gpu,gpu.d_sxz_2);
    solverFree2D(gpu,gpu.d_rho); solverFree2D(gpu,gpu.d_mu); solverFree2D(gpu,gpu.d_lambda); solverFree2D(gpu,gpu.d_lambda2mu);
    solverFree2D(gpu,gpu.d_dx); solverFree2D(gpu,gpu.d_dx_half); solverFree2D(gpu,gpu.d_dz); solverFree2D(gpu,gpu.d_dz_half);
    solverFree2D(gpu,gpu.d_wavelet); solverFree2D(gpu,gpu.d_rcv_grid_idx);
    solverFree2D(gpu,gpu.d_active_sources);solverFree2D(gpu,gpu.d_displayMaximum);

    // 释放检波器单步记录临时缓冲区
    solverFree2D(gpu,gpu.d_record_vx_step);
    solverFree2D(gpu,gpu.d_record_vz_step);

        solverFree2D(gpu,gpu.d_vx); solverFree2D(gpu,gpu.d_vz);
        solverFree2D(gpu,gpu.d_sigmaxx); solverFree2D(gpu,gpu.d_sigmazz); solverFree2D(gpu,gpu.d_sigmaxz);
        solverFree2D(gpu,gpu.d_mu_x_flat); solverFree2D(gpu,gpu.d_lambda_x_flat); solverFree2D(gpu,gpu.d_lambda2mu_x_flat);
        solverFree2D(gpu,gpu.d_mu_z_flat); solverFree2D(gpu,gpu.d_rho_x_z_flat); solverFree2D(gpu,gpu.d_rho_orig_flat);
        solverFree2D(gpu,gpu.d_dp_flat);
    gpu = GPUSimData{};
}

// 先准备新的检波器缓冲区，全部成功后才替换旧资源；空数组会清除旧采集布局。
void updateGpuReceivers(GPUSimData& gpu, const std::vector<int>& physicalIndices) {
    auto receiverIndices=physicalIndices;
    if(gpu.physicalNX>0) for(int& index:receiverIndices) {
        if(index<0||index>=gpu.physicalNX*gpu.physicalNZ) throw std::invalid_argument("Receiver outside physical model");
        index=(index/gpu.physicalNX+gpu.originZ)*gpu.NX+index%gpu.physicalNX+gpu.originX;
    }
    if (receiverIndices.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        throw std::invalid_argument("Too many receivers");
    }
    for (int index : receiverIndices) {
        if (index < 0 || index >= gpu.total_grid) {
            throw std::invalid_argument("Receiver index is outside the active GPU grid");
        }
    }
    int* newIndices = nullptr;
    float* newVx = nullptr;
    float* newVz = nullptr;
    try {
        if (!receiverIndices.empty()) {
            const size_t sampleBytes = receiverIndices.size() * sizeof(float);
            CUDA_CHECK(solverAllocate2D(gpu,&newIndices, receiverIndices.size() * sizeof(int)));
            CUDA_CHECK(solverCopy2D(gpu,newIndices, receiverIndices.data(), receiverIndices.size() * sizeof(int), cudaMemcpyHostToDevice));
            CUDA_CHECK(solverAllocate2D(gpu,&newVx, sampleBytes));
            CUDA_CHECK(solverAllocate2D(gpu,&newVz, sampleBytes));
            CUDA_CHECK(solverZero2D(gpu,newVx, 0, sampleBytes));
            CUDA_CHECK(solverZero2D(gpu,newVz, 0, sampleBytes));
        }
    }
    catch (...) {
        solverFree2D(gpu,newIndices); solverFree2D(gpu,newVx); solverFree2D(gpu,newVz);
        throw;
    }
    solverFree2D(gpu,gpu.d_rcv_grid_idx); solverFree2D(gpu,gpu.d_record_vx_step); solverFree2D(gpu,gpu.d_record_vz_step);
    gpu.d_rcv_grid_idx = newIndices;
    gpu.d_record_vx_step = newVx;
    gpu.d_record_vz_step = newVz;
    gpu.receiverCapacity = static_cast<int>(receiverIndices.size());
}

// Launches stress, velocity, source-injection, and boundary kernels for one time step.
void runGpuSimulationStep(GPUSimData& gpu, int current_it, const SimulationContext& ctx, int num_active_sources) {
    if (gpu.total_grid <= 0 || current_it < 0 || num_active_sources > kMaximumActiveSources) {
        throw std::invalid_argument("Invalid GPU simulation state, iteration, or source count");
    }
    dim3 block(16, 16);
    dim3 grid((gpu.NX + block.x - 1) / block.x, (gpu.NZ + block.y - 1) / block.y);

    // 同步主机端控制面板的震源类型及角度到 GPU 结构体
    gpu.src_type = ctx.src_type;
    gpu.src_angle = ctx.src_angle;
    if(gpu.cpu) {runCpuStep2D(gpu,current_it,ctx,num_active_sources);return;}

    if (gpu.flag_type == 1 || (gpu.flag_type == 3 && !gpu.upFlag)) {
        updateType1StressKernel << <grid, block >> > (gpu);
    }
    else if (gpu.flag_type == 2) {
        updateType2StressKernel << <grid, block >> > (gpu);
    }
    else if (gpu.flag_type == 3) {
        updateType3StressKernel << <grid, block >> > (gpu);
        if (gpu.upFlag) {
            mirrorType3StressKernel << <(gpu.NX + 255) / 256, 256 >> > (gpu);
        }
    }
    CUDA_CHECK(cudaGetLastError());

    if (gpu.flag_type == 1 || (gpu.flag_type == 3 && !gpu.upFlag)) {
        updateType1VelocityKernel << <grid, block >> > (gpu);
    }
    else if (gpu.flag_type == 2) {
        updateType2VelocityKernel << <grid, block >> > (gpu);
    }
    else if (gpu.flag_type == 3) {
        updateType3VelocityKernel << <grid, block >> > (gpu);
    }
    CUDA_CHECK(cudaGetLastError());

    // 3. Source Injection
    if (num_active_sources >= 0) {
        if (num_active_sources > 0) {
            injectMultipleSourcesKernel << <1, num_active_sources >> > (gpu, gpu.d_active_sources, num_active_sources);
        }
    }
    else {
        if (current_it < ctx.nt && current_it < static_cast<int>(ctx.wavelet.size())) {
            // 在注入时，直接将基础子波值乘以幅值增益系数 ctx.src_amp，实现零时延实时调控
            float wavelet_val = ctx.wavelet[current_it] * ctx.src_amp * kSourceInjectionScale2D;

            int sourceZ=ctx.src_z_idx+(gpu.physicalNX>0?gpu.originZ:0);
            int safe_src_z = (gpu.flag_type == 3 && gpu.upFlag) ? std::max(sourceZ, gpu.fs_idx + 1) : sourceZ;
            int src_idx = safe_src_z * gpu.NX + (gpu.physicalNX>0?ctx.src_idx%gpu.physicalNX+gpu.originX:ctx.src_idx%gpu.NX);

            injectSourceKernel << <1, 1 >> > (gpu, wavelet_val, src_idx);
        }
    }
    CUDA_CHECK(cudaGetLastError());

    int max_dim = std::max(gpu.NX, gpu.NZ);
    zeroBoundaryKernel << <(max_dim + 255) / 256, 256 >> > (gpu);
    CUDA_CHECK(cudaGetLastError());
}

// Copies the vertical-velocity component to host memory, recombining split fields when required.
static void copyFullWavefield2D(GPUSimData& gpu, float* h_vz_out) {
    if (!h_vz_out || gpu.total_grid <= 0) throw std::invalid_argument("Invalid wavefield output buffer");
    size_t bytes = gpu.total_grid * sizeof(float);
    if (gpu.flag_type == 1 || gpu.flag_type == 3) {
        std::vector<float> temp_v1(gpu.total_grid);
        std::vector<float> temp_v2(gpu.total_grid);
        CUDA_CHECK(solverCopy2D(gpu,temp_v1.data(), gpu.d_vz_1, bytes, cudaMemcpyDeviceToHost));
        CUDA_CHECK(solverCopy2D(gpu,temp_v2.data(), gpu.d_vz_2, bytes, cudaMemcpyDeviceToHost));
        for (int i = 0; i < gpu.total_grid; i++) {
            h_vz_out[i] = temp_v1[i] + temp_v2[i];
        }
    }
    else {
        CUDA_CHECK(solverCopy2D(gpu,h_vz_out, gpu.d_vz, bytes, cudaMemcpyDeviceToHost));
    }
}

void copyWavefieldToHost(GPUSimData& gpu,float* output) {
    if(!output) throw std::invalid_argument("Null physical wavefield output");
    if(gpu.physicalNX<=0) {copyFullWavefield2D(gpu,output);return;}
    std::vector<float> full(gpu.total_grid);copyFullWavefield2D(gpu,full.data());
    for(int z=0;z<gpu.physicalNZ;++z)
        std::copy_n(full.data()+(z+gpu.originZ)*gpu.NX+gpu.originX,gpu.physicalNX,output+z*gpu.physicalNX);
}

// Extracts receiver values for a specified time index into host trace arrays.
void copyReceiverRecordsFromGpu(GPUSimData& gpu, int num_rcv, int nt, int current_it, float* h_rec_vx, float* h_rec_vz) {
    if (num_rcv <= 0) return;
    if (num_rcv > gpu.receiverCapacity || nt <= 0 || current_it < 0 || current_it >= nt || !h_rec_vx || !h_rec_vz) {
        throw std::invalid_argument("Invalid receiver trace count, time index, or host output buffer");
    }

    if(gpu.cpu) {
        std::vector<float> vx(num_rcv),vz(num_rcv);recordCpuStep2D(gpu,num_rcv,vx.data(),vz.data());
        for(int r=0;r<num_rcv;++r) {h_rec_vx[size_t(r)*nt+current_it]=vx[r];h_rec_vz[size_t(r)*nt+current_it]=vz[r];}return;
    }
    int threads = 128;
    int blocks = (num_rcv + threads - 1) / threads;
    extractReceiverSamplesKernel << <blocks, threads >> > (
        gpu.d_vx_1, gpu.d_vx_2, gpu.d_vz_1, gpu.d_vz_2,
        gpu.d_vx, gpu.d_vz,
        gpu.d_rcv_grid_idx,
        gpu.d_record_vx_step,
        gpu.d_record_vz_step,
        num_rcv, gpu.flag_type, gpu.total_grid
        );

    std::vector<float> h_step_vx(num_rcv);
    std::vector<float> h_step_vz(num_rcv);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(solverCopy2D(gpu,h_step_vx.data(), gpu.d_record_vx_step, num_rcv * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(solverCopy2D(gpu,h_step_vz.data(), gpu.d_record_vz_step, num_rcv * sizeof(float), cudaMemcpyDeviceToHost));

    for (int r = 0; r < num_rcv; ++r) {
        h_rec_vx[static_cast<size_t>(r) * nt + current_it] = h_step_vx[r];
        h_rec_vz[static_cast<size_t>(r) * nt + current_it] = h_step_vz[r];
    }
}

// Converts the requested wavefield component to RGBA pixels for CUDA-OpenGL display.
void generateWavefieldTextureOnGpu(GPUSimData& gpu, uchar4* d_rgba_out, float color_scale, int show_component) {
    dim3 block(16, 16);
    dim3 grid((gpu.NX + block.x - 1) / block.x, (gpu.NZ + block.y - 1) / block.y);
    generateColorMapKernel << <grid, block >> > (gpu, d_rgba_out, color_scale, show_component,nullptr,nullptr);
    CUDA_CHECK(cudaGetLastError());
}

// Floating-point display transport avoids losing weak phases to 8-bit RGB encoding.
float generateScientificWavefield2D(GPUSimData& gpu,float4* output,float gain,int component,bool automatic) {
    if(gpu.cpu) return generateCpuDisplay2D(gpu,output,gain,component,automatic);
    dim3 block(16,16),grid((gpu.NX+15)/16,(gpu.NZ+15)/16);
    float reference=1.0f/std::max(gain,1e-6f);
    if(automatic) {
        CUDA_CHECK(solverZero2D(gpu,gpu.d_displayMaximum,0,sizeof(float)));
        generateColorMapKernel<<<grid,block>>>(gpu,nullptr,1,component,nullptr,reinterpret_cast<unsigned int*>(gpu.d_displayMaximum));
        CUDA_CHECK(cudaGetLastError());
        float peak=0;CUDA_CHECK(solverCopy2D(gpu,&peak,gpu.d_displayMaximum,sizeof(float),cudaMemcpyDeviceToHost));
        reference=std::max(peak,1e-30f)/std::max(gain/20.0f,1e-6f);
    }
    generateColorMapKernel<<<grid,block>>>(gpu,nullptr,gain > 0.0f ? 1.0f/reference : 0.0f,component,output,nullptr);
    CUDA_CHECK(cudaGetLastError());
    return reference;
}

// =============================================================================
// 【新增】： recordReceiverStepOnGpu 的主机端包装实现 (MSVC 与 NVCC 之间的安全桥梁)
// =============================================================================
// Extracts one receiver sample per channel and synchronously copies Vx and Vz arrays to host.
void recordReceiverStepOnGpu(GPUSimData& gpu, int num_rcv, float* h_out_vx, float* h_out_vz) {
    if (num_rcv <= 0) return;
    if (num_rcv > gpu.receiverCapacity || !h_out_vx || !h_out_vz) {
        throw std::invalid_argument("Invalid receiver count or host output buffer");
    }

    if(gpu.cpu) {recordCpuStep2D(gpu,num_rcv,h_out_vx,h_out_vz);return;}
    // 1. 在 GPU 端高并发提取当前时步下所有接收器的振幅
    int threads = 128;
    int blocks = (num_rcv + threads - 1) / threads;
    extractReceiverSamplesKernel << <blocks, threads >> > (
        gpu.d_vx_1, gpu.d_vx_2, gpu.d_vz_1, gpu.d_vz_2,
        gpu.d_vx, gpu.d_vz,
        gpu.d_rcv_grid_idx,
        gpu.d_record_vx_step,
        gpu.d_record_vz_step,
        num_rcv, gpu.flag_type, gpu.total_grid
        );

    // 2. 将 GPU 提取出的单步振幅拷回由 C++ 传入的 Host 临时数组中 (瞬间完成，无任何总线拥堵)
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(solverCopy2D(gpu,h_out_vx, gpu.d_record_vx_step, num_rcv * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(solverCopy2D(gpu,h_out_vz, gpu.d_record_vz_step, num_rcv * sizeof(float), cudaMemcpyDeviceToHost));
}

// =============================================================================
// 【解决循环依赖】：实现 Common.h 中声明的 ReplayManager::uploadFrameToGpu 成员函数
// =============================================================================
// Restores a cached replay frame into the active solver's vertical-velocity fields.
void ReplayManager::uploadFrameToGpu(int frameIdx, GPUSimData& gpu) {
    if (frameIdx < 0 || frameIdx >= static_cast<int>(frames.size())) return;
    const size_t expected=gpu.physicalNX>0?size_t(gpu.physicalNX)*gpu.physicalNZ:size_t(gpu.total_grid);
    if (frames[frameIdx].size() != expected) {
        throw std::invalid_argument("Replay frame dimensions do not match the active model");
    }
    std::vector<float> expanded;
    const float* replay=frames[frameIdx].data();
    if(gpu.physicalNX>0) {
        expanded.assign(gpu.total_grid,0);
        for(int z=0;z<gpu.physicalNZ;++z) std::copy_n(replay+z*gpu.physicalNX,gpu.physicalNX,expanded.data()+(z+gpu.originZ)*gpu.NX+gpu.originX);
        replay=expanded.data();
    }
    size_t bytes = gpu.total_grid * sizeof(float);

    if (gpu.flag_type == 1 || gpu.flag_type == 3) {
        // 覆盖 vz_1，并将 vz_2 抹零，使其和正好等于当前帧数据
        CUDA_CHECK(solverCopy2D(gpu,gpu.d_vz_1, replay, bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(solverZero2D(gpu,gpu.d_vz_2, 0, bytes));
    }
    else if (gpu.flag_type == 2) {
        CUDA_CHECK(solverCopy2D(gpu,gpu.d_vz, replay, bytes, cudaMemcpyHostToDevice));
    }
}
