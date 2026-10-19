#pragma once
#include "Common.h"
struct GpuInfo;
namespace Seis2D {
// Render the isolated 2D workspace, allocating solver resources only on first entry.
void renderSimulation2D(SimState& state,int width,int height,GLHandles& gl,const GpuInfo& info);
// Release only 2D-owned CUDA/GL/replay resources before changing routes or destroying GL.
void releaseSimulation2D(GLHandles& gl) noexcept;
// Unload optional NVML monitoring after workspace shutdown.
void shutdownDeviceMonitor2D() noexcept;
// Queue a field reset for the next 2D frame, retaining tuned display settings.
void requestFieldReset2D();
// Queue a view reset without changing the physical simulation.
void requestViewReset2D();
// Apply a shared interface theme accent without exposing private 2D state.
void setInterfaceAccent2D(const ImVec4& color);
// Configure a real small-grid CUDA capture for reproducible documentation screenshots.
void prepareDocumentation2D(GLHandles& gl,SimState& state);
// Freeze captured Vx/Vz samples for analysis, without changing acquired amplitudes.
void openCapturedRecord2D();
// Select documented viewer stages for deterministic offscreen screenshots.
void selectDocumentationView2D(int stage);
// Exercise continuation capture with the real solver after the documentation snapshot is frozen.
void checkRecordingTimeline2D(SimState& state,GLHandles& gl);
void checkExternalBoundaryView2D(bool visible);
// Capture real dock presets and archive one raw shot in the generated checks directory.
void configureDocumentationWorkspace2D(int stage);
// Exercise actual panel folding without modifying model or acquisition state.
void configureDocumentationGroups2D(int stage);
}
