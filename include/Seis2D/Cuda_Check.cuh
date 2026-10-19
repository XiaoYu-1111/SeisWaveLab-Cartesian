#pragma once
#include "Common.h"
#include "SolverNumerics.h"
#include <cuda_runtime.h>

// =============================================================================
// 1. GPU 硬件基础信息结构体 (C 兼容)
// =============================================================================
struct GpuInfo {
    char   name[256];
    int    computeCapabilityMajor;
    int    computeCapabilityMinor;
    size_t totalMem;
    int    multiProcessorCount; // SM 数量
    int    cudaCores;           // 计算出的 CUDA 核心数
    int    clockRate;           // 时钟频率
    bool   success;
};

// =============================================================================
// 2. C 兼容接口函数声明 (CUDA 硬件检测 / 简单测试 / 生命游戏模块)
// =============================================================================
#ifdef __cplusplus
extern "C" {
#endif

    // --- 硬件环境检测与诊断 ---
    /** Returns the active CUDA device properties, or a failed result if discovery fails. */
    GpuInfo getCudaDeviceInfo();
    /** Runs a small vector-add operation to verify CUDA allocation, launch, and copy paths. */
    bool runCudaTest(float* hostA, float* hostB, float* hostC, int elementCount);

    // --- 生命游戏 CUDA 模块 ---
    /** Initializes the CUDA random-state buffer used by the life-game demo. */
    void initializeCudaLife(int width, int height);
    /** Releases the legacy life-game random-state buffer, if allocated. */
    void releaseCudaLife();
    /** Seeds a life-game grid with the requested live-cell density. */
    void seedCudaLife(uint8_t* deviceWorld, int width, int height, float density);
    /** Advances the life-game cellular automaton and updates its heat map. */
    void updateCudaLife(uint8_t* deviceCurrent, uint8_t* deviceNext, float* deviceHeatMap,
        int width, int height, float deltaTime, bool paused, float decay, int birthMask, int survivalMask);
    /** Applies a circular paint or erase operation to the device grid. */
    void paintCudaLife(uint8_t* deviceWorld, float* deviceHeat, int width, int height,
        int mouseX, int mouseY, int radius, bool erase);
    /** Counts live cells in the life-game grid. */
    int getCudaLifePopulation(uint8_t* deviceWorld, int width, int height);

#ifdef __cplusplus
}
#endif

// =============================================================================
// 3. 辅助宏定义
// =============================================================================
// CUDA 失败时终止当前操作，让调用方清理资源，避免继续使用未分配的缓冲区。
inline void checkCudaResult(cudaError_t result, const char* operation, const char* file, int line) {
    if (result != cudaSuccess) {
        throw std::runtime_error(std::string("CUDA failure: ") + operation + " at " + file +
            ":" + std::to_string(line) + " | " + cudaGetErrorString(result));
    }
}
#define CUDA_CHECK(call) checkCudaResult((call), #call, __FILE__, __LINE__)

// =============================================================================
// 4. 地震波场模拟 GPU 显存资源管理结构体
// =============================================================================
struct GPUSimData {
    bool cpu=false;
    int workers=1;
    // 基础尺寸与控制参数
    int physicalNX=0, physicalNZ=0, originX=0, originZ=0;
    int   NX = 0;
    int   NZ = 0;
    int   total_grid = 0;
    float dt = 0.0f;
    float inv_dt = 0.0f;
    int   flag_type = 1;
    int   npml = 0;
    int   fs_idx = 0;
    bool  upFlag = false;

    // 空间有限差分系数
    float c1_h = 0.0f;
    float c2_h = 0.0f;
    float c3_h = 0.0f;
    float c4_h = 0.0f;

    // [分类指针组 1] 全局 PML 场分裂变量 (Type 1 & Type 3)
    float* d_vx_1 = nullptr; float* d_vx_2 = nullptr;
    float* d_vz_1 = nullptr; float* d_vz_2 = nullptr;
    float* d_sxx_1 = nullptr; float* d_sxx_2 = nullptr;
    float* d_szz_1 = nullptr; float* d_szz_2 = nullptr;
    float* d_sxz_1 = nullptr; float* d_sxz_2 = nullptr;

    // [分类指针组 2] Type 2 专用（非场分裂 PML 变量）
    float* d_vx = nullptr; float* d_vz = nullptr;
    float* d_sigmaxx = nullptr; float* d_sigmazz = nullptr; float* d_sigmaxz = nullptr;
    float* d_mu_x_flat = nullptr; float* d_lambda_x_flat = nullptr;
    float* d_lambda2mu_x_flat = nullptr; float* d_mu_z_flat = nullptr;
    float* d_rho_x_z_flat = nullptr; float* d_rho_orig_flat = nullptr;

    // [分类指针组 3] 基础物理介质参数
    float* d_rho = nullptr;
    float* d_mu = nullptr;
    float* d_lambda = nullptr;
    float* d_lambda2mu = nullptr;

    // [分类指针组 4] PML 边界衰减阻尼轮廓
    float* d_dx = nullptr; float* d_dx_half = nullptr;
    float* d_dz = nullptr; float* d_dz_half = nullptr;
    float* d_dp_flat = nullptr; // Type 3 专用

    // [分类指针组 5] 震源与检波器控制
    float* d_displayMaximum = nullptr;
    float* d_wavelet = nullptr;
    int* d_rcv_grid_idx = nullptr;
    float* d_record_vx_step = nullptr; // 大小：num_rcv * sizeof(float)
    float* d_record_vz_step = nullptr; // 大小：num_rcv * sizeof(float)

    GPUSource* d_active_sources = nullptr; // 活跃震源的显存缓冲区
    float      src_angle = 0.0f;
    int        src_type = 0; // <-- 【新增】：0:垂直集中力, 1:水平集中力, 2:膨胀源(纯P波), 3:剪切源(S波), 4:倾斜旋转力
    int        receiverCapacity = 0;
};

// =============================================================================
// 5. 地震波场 GPU 仿真计算核心接口函数
// =============================================================================
/** Allocates and initializes device fields and material arrays for one 2D simulation context. */
void initializeGpuSimulation(GPUSimData& gpuData, const SimulationContext& simulationContext, bool cpu=false);
/** Releases all device allocations owned by the simulation data object. */
void releaseGpuSimulation(GPUSimData& gpuData);
/** Validates host arrays, dimensions, material values, and boundary preconditions. */
void validateSimulationContext(const SimulationContext& simulationContext);
/** Replaces receiver device buffers atomically, including clearing an empty receiver set. */
void updateGpuReceivers(GPUSimData& gpuData, const std::vector<int>& receiverIndices);
/** Advances one finite-difference time step and injects active sources. */
void runGpuSimulationStep(GPUSimData& gpuData, int currentIteration,
    const SimulationContext& simulationContext, int activeSourceCount);
/** Copies the current wavefield component into a host buffer. */
void copyWavefieldToHost(GPUSimData& gpuData, float* hostVzOutput);
/** Copies recorded receiver samples from the device into host trace arrays. */
void copyReceiverRecordsFromGpu(GPUSimData& gpuData, int receiverCount, int timeSampleCount,
    int currentIteration, float* hostReceiverVx, float* hostReceiverVz);
/** Converts the selected wavefield component into an RGBA display texture on the GPU. */
void generateWavefieldTextureOnGpu(GPUSimData& gpuData, uchar4* deviceRgbaOutput,
    float colorScale, int componentIndex);

// --- 【新增：专用于数据采集单步捕获的 C++ 主机端包装接口】 ---
/** Extracts one time sample for every receiver and copies both velocity components to host buffers. */
void recordReceiverStepOnGpu(GPUSimData& gpuData, int receiverCount,
    float* hostVxOutput, float* hostVzOutput);

void uploadActiveSources2D(GPUSimData& gpu,const std::vector<GPUSource>& sources);

float generateScientificWavefield2D(GPUSimData& gpu,float4* output,float gain,int component,bool automatic);
