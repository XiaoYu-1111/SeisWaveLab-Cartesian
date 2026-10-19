#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <stdexcept>

constexpr int kStencilRadius = 4;
constexpr int kMaximumActiveSources = 200;
// Legacy numerical source scale; archive it explicitly rather than interpreting the UI multiplier as SI force.
constexpr float kSourceInjectionScale2D = 1e7f;

// 八阶交错网格一阶导数权重；依次作用于距半网格点 1/2、3/2、5/2、7/2 的样本。
constexpr std::array<double, 4> kStaggeredDerivativeWeights = {
    1225.0 / 1024.0, -245.0 / 3072.0, 49.0 / 5120.0, -5.0 / 7168.0
};
constexpr double kDerivativeWeightMagnitude =
    kStaggeredDerivativeWeights[0] - kStaggeredDerivativeWeights[1] +
    kStaggeredDerivativeWeights[2] - kStaggeredDerivativeWeights[3];
constexpr float kCourantLimit2d = static_cast<float>(1.0 / (1.4142135623730951 * kDerivativeWeightMagnitude));
constexpr float kSafeCourant2d = 0.480f;

// 在分配内存前检查当前二维内核的有符号 int 索引容量，乘法先提升为 size_t。
inline size_t checkedGridElementCount(int nx, int nz) {
    if (nx <= 2 * kStencilRadius || nz <= 2 * kStencilRadius) {
        throw std::invalid_argument("Grid dimensions must exceed the 8-cell stencil guard region");
    }
    const size_t elementCount = static_cast<size_t>(nx) * static_cast<size_t>(nz);
    if (elementCount > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        throw std::overflow_error("The 2D solver index capacity was exceeded");
    }
    return elementCount;
}

// 自由表面需要顶部镜像网格；npml=0 的全反射设置只适用于关闭自由表面的方案。
inline void validateBoundaryGeometry(int nx, int nz, int pmlWidth, bool freeSurface) {
    checkedGridElementCount(nx, nz);
    if (pmlWidth < 0 || pmlWidth > (nx - 2 * kStencilRadius - 1) / 2 ||
        pmlWidth > (nz - 2 * kStencilRadius - 1) / 2) {
        throw std::invalid_argument("PML width leaves no physical interior grid");
    }
    if (freeSurface && pmlWidth < kStencilRadius) {
        throw std::invalid_argument("A free surface requires at least 4 top ghost rows");
    }
}

// 将目标采样率对齐到整数计算步；导出必须使用 stride * dt 的实际时间间隔。
inline int getReceiverSamplingStride(double timeStep, double samplingRateHz) {
    if (!std::isfinite(timeStep) || timeStep <= 0.0 ||
        !std::isfinite(samplingRateHz) || samplingRateHz <= 0.0) {
        throw std::invalid_argument("Sampling rate and simulation time step must be finite and positive");
    }
    const double stride = std::round(1.0 / (samplingRateHz * timeStep));
    if (!std::isfinite(stride) || stride > (std::numeric_limits<int>::max)()) {
        throw std::overflow_error("Receiver sampling stride is too large");
    }
    return stride < 1.0 ? 1 : static_cast<int>(stride);
}
