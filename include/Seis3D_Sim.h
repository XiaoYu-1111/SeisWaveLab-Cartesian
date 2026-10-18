#pragma once
#include "Common3D.h"
#include <GL/glew.h>
#include <cuda_gl_interop.h>
#include <filesystem>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace Seis3D {

struct GLHandles3D {
    GLuint sliceProgram3D = 0, volumeProgram3D = 0, vao3D = 0;
    GLuint stagingBuffer3D = 0, volumeTexture3D = 0;
    GLuint volumeFramebuffer3D = 0, volumeDisplayTexture3D = 0;
    int volumeWidth3D = 640, volumeHeight3D = 640;
    GLuint scalarTextures3D[3]{}, displayTextures3D[3]{}, framebuffers3D[3]{};
    cudaGraphicsResource_t interop3D = nullptr;
    ViewLayout3D layout3D;
    std::array<float,40> renderedSettings3D{};
    bool rendered3D = false;
};

struct VolumeCamera3D {
    float yaw3D = 0.65f, pitch3D = 0.35f, distance3D = 1.65f;
    float pan3D[2]{};
};

// Each slice owns a normalized view; zoom changes presentation without allocating GPU storage.
struct SliceCamera3D {
    float zoom3D = 1.0f;
    float center3D[2] = {0.5f,0.5f};
};

struct Survey3D {
    int receiverCountX3D = 100, receiverCountY3D = 5;
    int receiverOrigin3D[3] = { 1, 2, 0 };
    int receiverSpacing3D[2] = { 3, 11 };
    int shotCountX3D = 1, shotCountY3D = 1;
    int shotOrigin3D[3] = { 24, 24, 12 };
    int shotSpacing3D[2] = { 20, 20 };
    double worldOrigin3D[2] = { 0.0, 0.0 };
    float cmpBinSize3D[2] = { 25.0f, 25.0f };
};

struct Gather3D {
    SimulationContext3D context3D;
    Source3D source3D;
    Survey3D survey3D;
    std::vector<Receiver3D> receivers3D;
    std::vector<float> samples3D; // [sample][receiver][vx,vy,vz].
    int sampleCount3D = 0, targetSamples3D = 1000, stride3D = 2, shotId3D = 1;
    int sampleIntervalUs3D = 0;
    int coordinateScalar3D = -1000;
    double firstSampleTime3D = 0.0;
};

// CPU-only imported shot: exact file coordinates/times remain independent from the simulation grid.
struct ImportedShot3D {
    Gather3D gather3D;
    std::vector<std::array<double,3>> coordinates3D;
    std::vector<double> firstTimes3D;
    std::vector<int> traceIds3D;
    int component3D=2;
    bool cartesian3D=true;
    std::string componentName3D="Amplitude", amplitudeUnit3D="file units", coordinateUnit3D="m", fileName3D;
};

// Bounded UI monitor, sampled once per compute batch; formal SEG-Y samples remain in Gather3D.
struct LiveTrace3D {
    static constexpr int capacity3D=1000;
    std::array<double,capacity3D> times3D{};
    std::vector<float> samples3D; // Ring [batch][receiver][Vx,Vy,Vz]; up to 6 MB of float samples at 500 receivers.
    int receiverCount3D=0, count3D=0, next3D=0;
};

// Preview-only gain compensation; recorded samples and SEG-Y export remain untouched.
struct TraceAgcSettings3D {
    bool enabled3D = false;
    float windowMs3D = 120.0f;
    float strength3D = 1.0f;
    float maxBoost3D = 50.0f;
    float noiseFloorPercent3D = 0.01f;
};

// A bounded, rendered trace preview; the underlying gathered samples retain their original units.
struct TraceSeries3D {
    std::vector<double> times3D, values3D;
    double peak3D=0, rawPeak3D=0;
};

// Cache frozen gather views so AGC on a long record is computed only when inputs change.
struct TracePreviewCache3D {
    std::unordered_map<int,TraceSeries3D> entries3D;
    const float* samples3D=nullptr;
    std::size_t valueCount3D=0;
    int sampleCount3D=0,receiverCount3D=0,sampleIntervalUs3D=0;
    double firstSampleTime3D=0;
    bool imported3D=false;
    TraceAgcSettings3D settings3D;
    std::vector<double> density3D;
    std::vector<int> densityReceivers3D;
    int densityRows3D=0,densityComponent3D=-1;
    bool densityNormalize3D=false;
    double densityMin3D=0,densityMax3D=0;
};

struct Seis3D_State {
    SimulationContext3D context3D, editContext3D;
    GPUSimData3D gpu3D;
    GLHandles3D gl3D;
    Source3D source3D;
    Survey3D survey3D;
    Gather3D gather3D;
    ImportedShot3D importedShot3D;
    LiveTrace3D liveTrace3D;
    TraceAgcSettings3D traceAgc3D;
    mutable TracePreviewCache3D tracePreviewCache3D;
    VolumeCamera3D camera3D;
    SliceCamera3D sliceCameras3D[3];
    std::vector<Receiver3D> receivers3D;
    bool ready3D = false, running3D = false, dirty3D = true;
    bool showVolume3D = true, autoScale3D = true, recording3D = false, sequence3D = false;
    bool homeRequested3D = false, singleStep3D = false;
    bool resetLayout3D = false, showGrid3D = true, showCrosshair3D = true;
    bool showBoundary3D = false; // Show actual external CPML samples, never alter solver boundary conditions.
    float fontScale3D = 1.0f, boundaryOpacity3D = 0.12f;
    bool panelsOpen3D[7] = {true,true,true,true,true,true,true};
    // Start with Paper scientific colors and display parameters while retaining signed Vz.
    int layoutPreset3D = 0, displayMode3D = 0, waveStyle3D = 1, modelStyle3D = 4;
    int visualPreset3D = 4, traceView3D = 0;
    int traceDisplay3D = 0, traceRow3D = 0, traceComponent3D = 2;
    int traceFirst3D=0, traceVisibleCount3D=100;
    bool traceShowAll3D=true; // Follow the current live/captured/imported selection's actual channel count.
    float traceGain3D = 1.0f;
    bool traceNormalizeEach3D = false, tracePositiveFill3D = true, traceAutoFit3D = true;
    bool traceShowWiggle3D=true,traceShowHeatmap3D=true,traceSignedFill3D=false,traceFitRequest3D=true;
    int traceStyle3D=3,tracePalette3D=1;
    float traceHeatmapRange3D=1,traceDensityOpacity3D=.85f;
    int materialComponent3D = 5, displayStride3D = 1;
    std::uint64_t displayedStep3D = 0;
    bool materialRangeAuto3D = true;
    float materialRange3D[2] = {0,1}, manualMaterialRange3D[2] = {1500,5000};
    float waveOpacity3D = 1.0f, waveThreshold3D = 0.008f;
    float waveGamma3D = 0.5f, materialBrightness3D = 1.0f;
    float modelOpacity3D = 0.16f, gridOpacity3D = 0.16f;
    int component3D = 0, stepsPerFrame3D = 2, slice3D[3] = {150,25,75}; // Display order: Vz, Vx, Vy, |V|, pressure.
    int targetSamples3D = 1000, samplingStride3D = 2, sequenceIndex3D = 0;
    int maximumSteps3D = 2000, selectedReceiver3D = 0;
    std::uint64_t step3D = 0;
    float viewMaximum3D = 1.0f, manualScale3D = 0.01f, gain3D = 1.8f;
    bool holdPeakScale3D = false;
    float scalePeak3D = 0;
    float referenceMaximum3D = 0;
    std::array<float,2> unitScalePeaks3D{};
    std::array<float,2> unitManualScales3D{.01f,1e6f};
    int scaleUnit3D=0; // Velocity m/s and pressure Pa keep separate references.
    int scaleComponent3D = -1;
    float volumeOpacity3D = 1.4f, volumeThreshold3D = 0.02f, computeMs3D = 0.0f;
    float volumeSampling3D = 1.25f, volumeSharpness3D = 1.15f;
    float volumeGain3D = 1.8f;
    bool volumeSizeCompensation3D = true;
    int volumeLook3D = 0;
    int volumeRequestedWidth3D = 640, volumeRequestedHeight3D = 640;
    double memoryRefreshTime3D = -1.0;
    std::size_t freeVram3D = 0, totalVram3D = 0;
    Receiver3D hoverPoint3D{-1,-1,-1};
    float hoverMaterial3D[3]{};
    bool hoverValid3D = false;
    char modelPath3D[1024]{};
    char modelExportPath3D[1024]{};
    char vtiModelExportPath3D[1024]{};
    char vtiWavefieldExportPath3D[1024]{};
    int modelExportFormat3D = 0; // S3DM and VTI retain independent output paths.
    char exportDirectory3D[1024]{};
    char exportPrefix3D[128]="shot";
    bool exportSessionFolder3D=true;
    char shotImportPath3D[1024]{};
    std::filesystem::path sequenceDirectory3D;
    std::string message3D = "Choose a model, then Initialize 3D. GPU resources are allocated on demand.";
    bool messageIsError3D = false;

    // Initialize CPU defaults and independent I/O paths without allocating GPU resources.
    Seis3D_State();
    // Destroy this sandbox before the application destroys its current GL context.
    ~Seis3D_State();
    Seis3D_State(const Seis3D_State&) = delete;
    Seis3D_State& operator=(const Seis3D_State&) = delete;
};

// Keep late-time roundoff and CPML tails from being promoted to full-strength colors by autoscaling.
inline float displayScale3D(const Seis3D_State& s) {
    const float scale=s.autoScale3D ? (s.holdPeakScale3D ? s.scalePeak3D : s.referenceMaximum3D) : s.manualScale3D;
    return std::isfinite(scale)&&scale>1e-12f?scale:1e-12f;
}

// Draw the complete independent workspace; return true when Home/Tab requests a cold route change.
bool renderScreen3D(Seis3D_State& state3D, int width3D, int height3D);
// Release CUDA/GL resources in the correct order; no global device reset and no 2-D state access.
void releaseSandbox3D(Seis3D_State& state3D) noexcept;

} // namespace Seis3D
