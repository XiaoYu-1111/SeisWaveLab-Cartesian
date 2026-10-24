#pragma once
#include "Common3D.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace Seis3D {
// Interchange profile: a single ASCII StructuredGrid piece, physical XYZ points,
// and scalar PointData Vp/Vs/Density in SI units. No complete volume is buffered.
class VtsModelReader3D {
public:
    VtsModelReader3D(const std::filesystem::path&, SimulationContext3D defaults);
    ~VtsModelReader3D();
    const SimulationContext3D& context() const;
    void readPlane(int z, float* interleavedVpVsDensity);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Writes only the initialized physical model, with atomic publication to a new path.
void writeVtsModel3D(const std::filesystem::path&, const SimulationContext3D&,
    const std::function<void(int, float*)>& readPlane);
}
