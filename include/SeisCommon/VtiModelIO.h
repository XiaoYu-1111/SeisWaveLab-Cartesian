#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace SeisCommon {
struct ModelGrid {
    int nx=0,ny=0,nz=0;
    std::array<double,3> origin{0,0,0},spacing{1,1,1};
    std::map<std::string,int> metadata;
};
// Shared physical-model I/O: XYZ point data Vp/Vs/Density, SI units, X fastest.
// A 2-D X-Z model uses ny=1. Readers validate before callers replace live state.
class VtiModelReader {
public:
    explicit VtiModelReader(const std::filesystem::path&,std::size_t maxPoints=36000000,
        std::function<void(float)> progress={});
    ~VtiModelReader();
    const ModelGrid& grid() const;
    float maximumVp() const;
    void readPlane(int z,float* interleavedVpVsDensity);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Compact, uncompressed little-endian Float32 raw-appended VTI, with UInt64 blocks.
// Publication uses a private temporary directory and never overwrites existing files.
void writeVtiModel(const std::filesystem::path&,const ModelGrid&,
    const std::function<void(int,float*)>& readPlane);
}
