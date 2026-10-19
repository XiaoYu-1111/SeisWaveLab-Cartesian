#include "Cuda_Check.cuh"
#include "ExternalDomain2D.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

// 失败时终止当前算例，确保检查在 Release 构建中也始终执行。
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// 检查非法配置被明确拒绝，而不是触发 GPU 访问越界。
static void requireRejected(const std::function<void()>& operation) {
    bool rejected = false;
    try { operation(); }
    catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid input was not rejected");
}

// 创建不依赖 ImGui 或全局编辑参数的均匀二维介质。
static SimulationContext makeContext(int scheme, bool freeSurface = false, int size = 96, float density = 1000.0f) {
    SimulationContext ctx{};
    ctx.NX = size; ctx.NZ = size;
    ctx.total_grid = static_cast<int>(checkedGridElementCount(size, size));
    ctx.h = 1.0f; ctx.dt = 0.0001f; ctx.nt = 700; ctx.npml = 16;
    ctx.flag_type = scheme; ctx.upFlag = freeSurface;
    ctx.c1_h = static_cast<float>(kStaggeredDerivativeWeights[0]);
    ctx.c2_h = static_cast<float>(kStaggeredDerivativeWeights[1]);
    ctx.c3_h = static_cast<float>(kStaggeredDerivativeWeights[2]);
    ctx.c4_h = static_cast<float>(kStaggeredDerivativeWeights[3]);
    ctx.rho.assign(ctx.total_grid, density);
    ctx.mu.assign(ctx.total_grid, density * 1000.0f * 1000.0f);
    ctx.lambda2mu.assign(ctx.total_grid, density * 2000.0f * 2000.0f);
    ctx.lambda.assign(ctx.total_grid, density * 2000000.0f);
    ctx.dp_flat.assign(size, density * 3000000.0f);
    ctx.dx.assign(size, 0.0f); ctx.dx_half.assign(size, 0.0f);
    ctx.dz.assign(size, 0.0f); ctx.dz_half.assign(size, 0.0f);
    ctx.wavelet.resize(ctx.nt);
    for (int n = 0; n < ctx.nt; ++n) {
        const float phase = 3.14159265358979323846f * 200.0f * (n * ctx.dt - 0.005f);
        ctx.wavelet[n] = (1.0f - 2.0f * phase * phase) * std::exp(-phase * phase);
    }
    ctx.src_idx = (size / 2) * size + size / 2;
    ctx.src_z_idx = size / 2;
    ctx.src_amp = 1.0f; ctx.src_type = 0;
    ctx.num_rcv = 1;
    ctx.rcv_grid_idx = {ctx.src_idx + 8};
    return ctx;
}

// 测试对象负责释放生产求解器的显存，异常路径也执行清理。
struct DeviceSimulation {
    GPUSimData data;
    explicit DeviceSimulation(const SimulationContext& ctx) { initializeGpuSimulation(data, ctx); }
    ~DeviceSimulation() { releaseGpuSimulation(data); }
};

// 直接读取设备数组，供独立解析式检查使用。
static std::vector<float> readField(const float* deviceData, int count) {
    std::vector<float> values(count);
    CUDA_CHECK(cudaMemcpy(values.data(), deviceData, values.size() * sizeof(float), cudaMemcpyDeviceToHost));
    return values;
}

// 验证八阶交错导数对一次、三次、五次和七次多项式的矩条件。
static void checkPolynomialMoments() {
    for (int degree = 1; degree <= 7; degree += 2) {
        double moment = 0.0;
        for (size_t i = 0; i < kStaggeredDerivativeWeights.size(); ++i) {
            moment += 2.0 * kStaggeredDerivativeWeights[i] * std::pow(i + 0.5, degree);
        }
        require(std::abs(moment - (degree == 1 ? 1.0 : 0.0)) < 1e-10, "Stencil polynomial consistency failed");
    }
    requireRejected([] { checkedGridElementCount(100000, 100000); });
    requireRejected([] { validateBoundaryGeometry(96, 96, 0, true); });
    requireRejected([] { validateBoundaryGeometry(96, 96, 48, false); });
    requireRejected([] { checkedGridElementCount(8, 96); });
    require(getReceiverSamplingStride(0.00024, 10000.0) == 1, "Sampling above simulation rate was not aligned");
    require(getReceiverSamplingStride(0.00024, 1000.0) == 4, "Sampling stride rounding failed");
    requireRejected([] { getReceiverSamplingStride(0.0, 1000.0); });
    requireRejected([] { getReceiverSamplingStride(0.001, 0.0); });
}

// 用解析三次应力梯度检查垂向差分；覆盖低密度介质及自由表面方案的高阶邻点。
static void checkCubicStressGradient(int scheme, float density, bool freeSurface) {
    SimulationContext ctx = makeContext(scheme, freeSurface, 96, density);
    DeviceSimulation simulation(ctx);
    std::vector<float> stress(ctx.total_grid);
    for (int z = 0; z < ctx.NZ; ++z) {
        for (int x = 0; x < ctx.NX; ++x) stress[z * ctx.NX + x] = static_cast<float>(z * z * z);
    }
    float* stressField = scheme == 2 ? simulation.data.d_sigmazz : simulation.data.d_szz_1;
    CUDA_CHECK(cudaMemcpy(stressField, stress.data(), stress.size() * sizeof(float), cudaMemcpyHostToDevice));
    runGpuSimulationStep(simulation.data, 0, ctx, 0);
    std::vector<float> vz(ctx.total_grid);
    copyWavefieldToHost(simulation.data, vz.data());
    const int z = 30, x = 48;
    const float expected = ctx.dt * 3.0f * (z + 0.5f) * (z + 0.5f) / density;
    require(std::abs(vz[z * ctx.NX + x] - expected) < expected * 2e-5f, "Cubic stress derivative disagrees with analytic velocity");
    if (freeSurface) {
        const auto normalStress = readField(simulation.data.d_szz_1, ctx.total_grid);
        for (int column = 4; column < ctx.NX - 4; ++column) {
            require(normalStress[ctx.npml * ctx.NX + column] == 0.0f, "Normal traction is nonzero on the free surface");
            for (int layer = 1; layer <= 4; ++layer) {
                require(normalStress[(ctx.npml - layer) * ctx.NX + column] ==
                    -normalStress[(ctx.npml + layer) * ctx.NX + column], "Normal-stress ghost parity failed");
            }
        }
    }
}

// 关闭自由表面后，Type 3 必须逐步等价于全域分裂 PML，包含顶部区域。
static void checkDisabledFreeSurface() {
    SimulationContext first = makeContext(1), third = makeContext(3, false);
    first.src_z_idx = third.src_z_idx = 12;
    first.src_idx = third.src_idx = 12 * first.NX + 48;
    DeviceSimulation a(first), b(third);
    for (int n = 0; n < 120; ++n) {
        runGpuSimulationStep(a.data, n, first, -1);
        runGpuSimulationStep(b.data, n, third, -1);
    }
    std::vector<float> av(first.total_grid), bv(first.total_grid);
    copyWavefieldToHost(a.data, av.data()); copyWavefieldToHost(b.data, bv.data());
    require(av == bv, "Disabled free surface differs from whole-domain PML");
}

// 验证重叠震源不会丢失累加、震源类型一致，并拒绝过量震源和非法检波器。
static void checkSourcesAndResources() {
    SimulationContext ctx = makeContext(1);
    ctx.src_type = 1;
    DeviceSimulation simulation(ctx);
    std::vector<GPUSource> sources(kMaximumActiveSources, {ctx.src_idx, 0.005f, 200.0f, 1.0f});
    CUDA_CHECK(cudaMemcpy(simulation.data.d_active_sources, sources.data(), sources.size() * sizeof(GPUSource), cudaMemcpyHostToDevice));
    runGpuSimulationStep(simulation.data, 0, ctx, kMaximumActiveSources);
    const auto vx = readField(simulation.data.d_vx_1, ctx.total_grid);
    const float expected = kMaximumActiveSources * 1e7f * ctx.dt / ctx.rho[ctx.src_idx];
    require(std::abs(vx[ctx.src_idx] - expected) < expected * 1e-5f, "Overlapping source contributions were lost");
    requireRejected([&] { runGpuSimulationStep(simulation.data, 0, ctx, kMaximumActiveSources + 1); });
    requireRejected([&] { updateGpuReceivers(simulation.data, {ctx.total_grid}); });
    require(simulation.data.receiverCapacity == 1, "Failed receiver update damaged prior buffers");
    const int invalidIndex = ctx.total_grid;
    CUDA_CHECK(cudaMemcpy(simulation.data.d_rcv_grid_idx, &invalidIndex, sizeof(int), cudaMemcpyHostToDevice));
    float invalidVx = -1.0f, invalidVz = -1.0f;
    recordReceiverStepOnGpu(simulation.data, 1, &invalidVx, &invalidVz);
    require(invalidVx == 0.0f && invalidVz == 0.0f, "Invalid device receiver index was not contained");
    ReplayManager replay;
    replay.frames.emplace_back(3, 0.0f);
    requireRejected([&] { replay.uploadFrameToGpu(0, simulation.data); });
    updateGpuReceivers(simulation.data, {});
    require(simulation.data.receiverCapacity == 0 && !simulation.data.d_rcv_grid_idx, "Empty receiver set was not cleared");
    float output = 0.0f;
    requireRejected([&] { recordReceiverStepOnGpu(simulation.data, 1, &output, &output); });
    releaseGpuSimulation(simulation.data); releaseGpuSimulation(simulation.data);
    require(simulation.data.total_grid == 0 && !simulation.data.d_vx_1, "Released resources were not reset");
    ctx.rho[0] = 0.0f;
    requireRejected([&] { validateSimulationContext(ctx); });
    ctx = makeContext(1); ctx.dt = 1.0f;
    requireRejected([&] { validateSimulationContext(ctx); });
    ctx = makeContext(1); ctx.mu[0] = ctx.lambda2mu[0];
    requireRejected([&] { validateSimulationContext(ctx); });
}

// 在不经过界面的条件下执行生产求解器，并收集单点垂向记录。
static std::vector<float> runTrace(const SimulationContext& ctx) {
    DeviceSimulation simulation(ctx);
    std::vector<float> trace(ctx.nt);
    for (int n = 0; n < ctx.nt; ++n) {
        runGpuSimulationStep(simulation.data, n, ctx, -1);
        float vx = 0.0f;
        recordReceiverStepOnGpu(simulation.data, 1, &vx, &trace[n]);
        require(std::isfinite(trace[n]) && std::isfinite(vx), "Non-finite receiver sample");
    }
    std::vector<float> field(ctx.total_grid);
    copyWavefieldToHost(simulation.data, field.data());
    for (float value : field) require(std::isfinite(value), "Non-finite wavefield sample");
    return trace;
}

// 与更大域参考记录比较晚到误差，检验均匀介质正入射附近的 PML 吸收效果。
static void checkPmlReflection(int scheme) {
    SimulationContext absorbing = makeContext(scheme);
    const float maximumDamping = (3.0f * 2000.0f * 16.12f * 3.5f) / (2.0f * absorbing.npml);
    for (int i = 0; i < absorbing.npml; ++i) {
        const float whole = maximumDamping * std::pow((absorbing.npml - i) / float(absorbing.npml), 2.5f);
        const float half = maximumDamping * std::pow((absorbing.npml - i - 0.5f) / float(absorbing.npml), 2.5f);
        absorbing.dx[i] = absorbing.dx[absorbing.NX - 1 - i] = whole;
        absorbing.dz[i] = absorbing.dz[absorbing.NZ - 1 - i] = whole;
        absorbing.dx_half[i] = absorbing.dx_half[absorbing.NX - 1 - i] = half;
        absorbing.dz_half[i] = absorbing.dz_half[absorbing.NZ - 1 - i] = half;
    }
    SimulationContext reflecting = makeContext(scheme);
    SimulationContext reference = makeContext(scheme, false, 288);
    const auto pmlTrace = runTrace(absorbing);
    const auto wallTrace = runTrace(reflecting);
    const auto referenceTrace = runTrace(reference);
    double pmlError = 0.0, wallError = 0.0;
    for (int n = 350; n < absorbing.nt; ++n) {
        pmlError += std::pow(double(pmlTrace[n]) - referenceTrace[n], 2);
        wallError += std::pow(double(wallTrace[n]) - referenceTrace[n], 2);
    }
    const double ratio = std::sqrt(pmlError / wallError);
    std::cout << "PML scheme " << scheme << ": late-window error ratio vs reflecting boundary = " << ratio << '\n';
    require(wallError > 0.0 && std::isfinite(ratio) && ratio < 0.5, "PML does not sufficiently reduce this homogeneous-case reflected error");
}

static void checkExternalAbsorber2D() {
    for(int scheme=1;scheme<=3;++scheme) {
        auto physical=makeContext(scheme,scheme==3,48);
        physical.externalPml=true;physical.src_z_idx=2;physical.src_idx=2*physical.NX+2;
        physical.rcv_grid_idx={0,physical.NX-1,(physical.NZ-1)*physical.NX,physical.src_idx+1};physical.num_rcv=4;
        auto expanded=extendedContext2D(physical);
        const int pad=physical.npml;
        require(physical.NX==48&&expanded.NX==80,"Absorber consumed physical cells");
        for(int z=0;z<physical.NZ;++z) for(int x=0;x<physical.NX;++x) {
            const int k=(z+pad)*expanded.NX+x+pad;
            require(expanded.rho[k]==physical.rho[z*physical.NX+x],"Physical material changed during extension");
            require(expanded.dx[x+pad]==0&&expanded.dz[z+pad]==0,"Damping entered physical domain");
        }
        DeviceSimulation a(physical),b(expanded);
        updateGpuReceivers(a.data,physical.rcv_grid_idx);
        std::vector<int> indices(4);CUDA_CHECK(cudaMemcpy(indices.data(),a.data.d_rcv_grid_idx,4*sizeof(int),cudaMemcpyDeviceToHost));
        require(indices==expanded.rcv_grid_idx,"Physical receiver indices were not translated");
        for(int n=0;n<40;++n) {runGpuSimulationStep(a.data,n,physical,-1);runGpuSimulationStep(b.data,n,expanded,-1);}
        std::vector<float> model(physical.total_grid),full(expanded.total_grid);
        copyWavefieldToHost(a.data,model.data());copyWavefieldToHost(b.data,full.data());
        float peak=0;for(int z=0;z<physical.NZ;++z) for(int x=0;x<physical.NX;++x) {
            require(std::abs(model[z*physical.NX+x]-full[(z+pad)*expanded.NX+x+pad])<1e-8f,"External-domain source or cropped wavefield differs from reference");
            peak=std::max(peak,std::abs(model[z*physical.NX+x]));
        }
        require(peak>0,"Near-edge physical source was not injected");
        float4* display=nullptr;CUDA_CHECK(cudaMalloc(&display,size_t(a.data.total_grid)*sizeof(float4)));
        const float range=generateScientificWavefield2D(a.data,display,20,2,true);
        std::vector<float4> pixels(a.data.total_grid);CUDA_CHECK(cudaMemcpy(pixels.data(),display,pixels.size()*sizeof(float4),cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaFree(display));
        require(std::abs(range-peak)<=peak*1e-5f,"Automatic display scale includes external ghost/PML cells or uses quantized samples");
        for(int z=0;z<physical.NZ;++z) for(int x=0;x<physical.NX;++x) {
            const auto pixel=pixels[(z+pad)*expanded.NX+x+pad];
            require(std::abs(pixel.y-model[z*physical.NX+x]/range)<2e-5f,"Float display transport changed normalized amplitude");
        }
        std::vector<float> unchanged(model.size());copyWavefieldToHost(a.data,unchanged.data());
        require(unchanged==model,"Display normalization modified physical amplitudes");
        GPUSource source{};source.idx=physical.src_idx;source.amp=1;source.f_peak=100;source.t=.005f;
        uploadActiveSources2D(a.data,{source});GPUSource mapped{};
        CUDA_CHECK(cudaMemcpy(&mapped,a.data.d_active_sources,sizeof(mapped),cudaMemcpyDeviceToHost));
        require(mapped.idx==expanded.src_idx,"Continuous source index not translated");
        ReplayManager replay;replay.frames.push_back(model);replay.uploadFrameToGpu(0,a.data);
        std::vector<float> replayed(model.size());copyWavefieldToHost(a.data,replayed.data());
        require(replayed==model,"Physical-domain replay failed to restore the padded GPU fields");
    }
    std::cout<<"PASS: external 2D absorber, untouched physical grid, edge sources, receiver translation and physical replay.\n";
}

static void checkCpuBackend2D(bool compareCuda) {
 for(int scheme=1;scheme<=3;++scheme) {
  auto ctx=makeContext(scheme,scheme==3,48);ctx.externalPml=true;
  GPUSimData cpu,cuda,serial;
  struct Cleanup {GPUSimData& a;GPUSimData& b;GPUSimData& c;~Cleanup(){releaseGpuSimulation(a);releaseGpuSimulation(b);releaseGpuSimulation(c);}} cleanup{cpu,cuda,serial};
  initializeGpuSimulation(cpu,ctx,true);cpu.workers=4;
  initializeGpuSimulation(serial,ctx,true);serial.workers=1;
  if(compareCuda) initializeGpuSimulation(cuda,ctx);
  for(int n=0;n<80;++n) {
   runGpuSimulationStep(cpu,n,ctx,-1);runGpuSimulationStep(serial,n,ctx,-1);
   if(compareCuda) runGpuSimulationStep(cuda,n,ctx,-1);
  }
  std::vector<float> a(ctx.total_grid),b(a.size()),c(a.size());copyWavefieldToHost(cpu,a.data());copyWavefieldToHost(serial,c.data());
  require(a==c,"CPU parallel wavefield differs from serial reference");
  double norm=0,diff=0;for(float value:a) {require(std::isfinite(value),"CPU non-finite wavefield");norm+=double(value)*value;}
  require(norm>0,"CPU source was not injected");
  if(compareCuda) {
   copyWavefieldToHost(cuda,b.data());for(size_t k=0;k<a.size();++k) diff+=std::pow(double(a[k])-b[k],2);
   require(std::sqrt(diff/norm)<2e-4,"CPU/CUDA wavefield mismatch");
   float ax,az,bx,bz;recordReceiverStepOnGpu(cpu,1,&ax,&az);recordReceiverStepOnGpu(cuda,1,&bx,&bz);
   require(std::abs(az-bz)<1e-4f*std::sqrt(norm),"CPU/CUDA receiver mismatch");
  }
  std::vector<float4> pixels(cpu.total_grid);generateScientificWavefield2D(cpu,pixels.data(),0,2,true);
  for(auto pixel:pixels) require(pixel.y==0,"Zero display gain is not zero on CPU");
  std::vector<float> unchanged(a.size());copyWavefieldToHost(cpu,unchanged.data());require(a==unchanged,"CPU display modified wavefield");
  ReplayManager replay;replay.frames.push_back(a);replay.uploadFrameToGpu(0,cpu);copyWavefieldToHost(cpu,b.data());require(a==b,"CPU replay roundtrip failed");
  updateGpuReceivers(cpu,{});require(cpu.receiverCapacity==0,"CPU receiver cleanup failed");
 }
 std::cout<<"PASS: CPU serial/OpenMP 1/2/3, external PML, sources, receiver gather, zero display gain and replay"<<(compareCuda?", CUDA parity":"")<<".\n";
}

// 无 CUDA 设备时明确跳过 GPU 算例，不能把未执行的检查记为通过。
int main() {
    try {
        checkPolynomialMoments();
        int deviceCount = 0;
        const bool cudaAvailable=cudaGetDeviceCount(&deviceCount)==cudaSuccess&&deviceCount>0;
        checkCpuBackend2D(cudaAvailable);
        if (!cudaAvailable) return 77;
        checkExternalAbsorber2D();
        checkCubicStressGradient(1, 1000.0f, false);
        checkCubicStressGradient(2, 1000.0f, false);
        checkCubicStressGradient(2, 100.0f, false);
        checkCubicStressGradient(2, 0.5f, false);
        checkCubicStressGradient(3, 1000.0f, true);
        checkDisabledFreeSurface();
        checkSourcesAndResources();
        checkPmlReflection(1);
        checkPmlReflection(2);
        runTrace(makeContext(3, true));
        CUDA_CHECK(cudaDeviceSynchronize());
        std::cout << "PASS: stencil, bounds/CFL, analytic gradients, free-surface parity/toggle, "
            "overlapping sources, receiver lifecycle, repeated release, finite evolution, and PML reference comparisons.\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
