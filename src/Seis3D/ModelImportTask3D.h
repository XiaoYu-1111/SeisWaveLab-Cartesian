#pragma once
#include "Seis3D_Sim.h"
#include <atomic>
#include <chrono>
#include <future>

namespace Seis3D {
// The worker owns CPU materials and private compute allocations; it never touches live UI/GL state.
struct ModelImportTask3D {
    std::atomic<float> progress{0};
    std::atomic<int> stage{0}; // validate, read, allocate, upload, display
    std::atomic<bool> cancel{false};
    int phase=0,backend=0,workers=1,device=0;
    bool cudaAvailable=false,forceCpu=false;
    SimulationContext3D context;
    GPUSimData3D gpu;
    std::filesystem::path path;
    std::string status;
    std::vector<float> materials;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    std::future<void> worker;
    ~ModelImportTask3D();
};
}
