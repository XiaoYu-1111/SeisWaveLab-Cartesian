#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <direct.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "Seis_Sim.h"
#include "SeismicViewer2D.h"
#include "Cuda_Check.cuh"
#include "CpuSolver2D.h"
#include "ExternalDomain2D.h"
#include "WavePalettes.h"
#include "SeismicIO.h"
#include "ResourcePaths.h"
#include "NativeFileDialog.h"
#include "AppWidgets.h"
#include "AppShortcuts.h"
#include "AppTheme.h"
#include "AppInfo.h"
#include "ShotExport2D.h"
#include "VtiModelIO.h"
#include "imgui_internal.h"
#include "implot.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <numeric>

namespace Seis2D::Legacy2D {
// Build host physical arrays using the established 2D solver setup.
void setupTestContext(SimulationContext& context,const Parameters& parameters);
// Validate source sampling before constructing the Ricker time series.
void generateRickerWavelet(std::vector<float>& wavelet,int nt,float dt,float f0,float t0);
#include "State2D.inc"
#include "DockWorkspace2D.inc"
#include "ParameterGroups2D.inc"

// Export and inspect only samples that have actually been acquired, excluding unfilled buffer tails.
std::vector<std::vector<float>> capturedTraceCopy2D(const std::vector<std::vector<float>>& traces,int samples) {
    if(samples<1||traces.empty()) throw std::runtime_error("No acquired 2D samples are available.");
    std::vector<std::vector<float>> result;result.reserve(traces.size());
    for(const auto& trace:traces) {
        if(samples>int(trace.size())) throw std::runtime_error("2D capture sample count exceeds its buffer.");
        result.emplace_back(trace.begin(),trace.begin()+samples);
    }
    return result;
}
// Freeze both physical velocity components; subsequent solver edits cannot alter the viewer snapshot.
void snapshotCaptureForViewer2D() {
    if(acquisition2D.capturedSamples<1) return;
    try {
        RecordData2D vz,vx;
        vz.traces=capturedTraceCopy2D(acquisition2D.recorded_vz,acquisition2D.capturedSamples);
        vx.traces=capturedTraceCopy2D(acquisition2D.recorded_vx,acquisition2D.capturedSamples);
        vz.firstTimes.assign(vz.traces.size(),0);vx.firstTimes=vz.firstTimes;
        vz.sampleInterval=vx.sampleInterval=acquisition2D.actualSampleInterval;
        vz.units=vx.units="m/s";vz.name="2D capture / Vz";vx.name="2D capture / Vx";
        recordViewer2D.records[0]=std::move(vz);recordViewer2D.records[1]=std::move(vx);
        recordViewer2D.dualComponent=true;++recordViewer2D.revision;recordViewer2D.cache=ViewerCache2D{};
        recordViewer2D.fitRequest=true;recordViewer2D.error=false;recordViewer2D.status="Captured 2D samples copied for display. Gains retained.";
    } catch(const std::exception& error) {recordViewer2D.status=error.what();recordViewer2D.error=true;}
}
#include "Material2D.inc"
#include "Canvas2D.inc"
#include "Numerics2D.inc"
#include "ModelPresets2D.inc"
#include "ModelIo2D.inc"
#include "ExportPanel2D.inc"
#include "Acquisition2D.inc"
#include "Workflow2D.inc"
#include "ParameterPanel2D.inc"
#include "Performance2D.inc"
#include "Workspace2D.inc"
}

namespace Seis2D {
// Keep application routing independent from the 2D solver's private mutable state.
void renderSimulation2D(SimState& state,int width,int height,GLHandles& gl,const GpuInfo& info) {
    AppUi::ThemePanelScope theme;
    Legacy2D::exportDevice2D=Legacy2D::gpuData2D.cpu?"CPU / OpenMP":info.name;
    Legacy2D::renderSeisSimScreenGpu(state,width,height,gl,info);
}
// A cold switch releases the 2D stream and interop images before the shared GL context disappears.
void releaseSimulation2D(GLHandles& gl) noexcept {
    using namespace Legacy2D;
    acquisition2D.isRecording=false;autoAcquisition2D.state=AUTO_ACQ_IDLE;replay2D.clear();replay2D.state=REPLAY_IDLE;
    benchmark2D.clear();releaseGpuSimulation(gpuData2D);initialized2D=false;
    if(gl.cudaSeisRes) cudaGraphicsUnregisterResource(gl.cudaSeisRes);
    gl.cudaSeisRes=nullptr;gl.cudaSeisArray=nullptr;
    cudaFree(gl.deviceWavefieldRgba);gl.deviceWavefieldRgba=nullptr;gl.wavefieldRgbaCapacity=0;
    if(gl.seisTex) glDeleteTextures(1,&gl.seisTex);gl.seisTex=0;
    if(modelTex2D) glDeleteTextures(1,&modelTex2D);modelTex2D=0;
    releaseCanvasTarget2D();
    currentIt2D=0;elapsedRunWallTime2D=0;resetSimRequested2D=resetViewportRequested2D=false;
}
// Optional hardware telemetry is unloaded separately from simulation resources.
void shutdownDeviceMonitor2D() noexcept {Legacy2D::deviceMonitor2D.unload();}
// Keyboard commands queue changes through the same private state as the 2D panel.
void requestFieldReset2D() {Legacy2D::resetSimRequested2D=true;}
// Camera reset preserves the model, physical fields and receiver records.
void requestViewReset2D() {Legacy2D::resetViewportRequested2D=true;}
// Keep theme changes and the legacy 2D accent editor connected through an explicit interface.
void setInterfaceAccent2D(const ImVec4& color) {Legacy2D::uiAccent2D=color;}
// Open the latest fixed snapshot without changing display gain or AGC settings.
void openCapturedRecord2D() {Legacy2D::snapshotCaptureForViewer2D();Legacy2D::recordViewer2D.isOpen=true;Legacy2D::recordViewer2D.focusRequest=true;}
// Use genuine CUDA solver samples for screenshots; no synthetic waveform is substituted.
void prepareDocumentation2D(GLHandles& gl,SimState& state) {
    using namespace Legacy2D;
    gridWidth2D=256;gridHeight2D=160;sourceFrequency2D=60;sourceDelay2D=1/sourceFrequency2D;stepsPerFrame2D=20;
    parameters2D.FDM.npml=12;parameters2D.FDM.dt=.0001f;parameters2D.FDM.nt=3000;
    initializeSeismicSimulation(gl);loadScenario(SCENE_TWO_LAYER);updateModelTexture();uploadMaterialToGpu();
    // Exercise the real save/load path before acquiring documentation samples on either backend.
    const auto oldRho=simulationContext2D.rho,oldMu=simulationContext2D.mu,oldL2m=simulationContext2D.lambda2mu;
    const int oldX=editSrcX2D,oldZ=editSrcZ2D;
    const auto modelFile=getExecutableDirectory().parent_path().parent_path()/"checks/vti-2d"/
        ("model-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".vti");
    exportVtiModel2D(modelFile);importVtiModel2D(modelFile,gl,state);
    for(std::size_t q=0;q<oldRho.size();++q) {
        const auto close=[](float a,float b){return std::abs(a-b)<=std::max(1.f,std::abs(a))*3e-6f;};
        if(!close(oldRho[q],simulationContext2D.rho[q])||!close(oldMu[q],simulationContext2D.mu[q])||!close(oldL2m[q],simulationContext2D.lambda2mu[q]))
            throw std::runtime_error("2D VTI save/load changed physical material or depth ordering.");
    }
    editSrcX2D=oldX;editSrcZ2D=oldZ;resetSimRequested2D=false;
    std::cout<<"PASS: real "<<(gpuData2D.cpu?"CPU":"CUDA")<<" 2D VTI model save/load preserves physical materials, spacing and Z order.\n";
    acquisition2D.numChannels=100;acquisition2D.startX=16;acquisition2D.endX=239;acquisition2D.receiverDepth=14;
    acquisition2D.receivers.clear();
    for(int i=0;i<100;++i) acquisition2D.receivers.push_back({16+int(std::round(223.0*i/99)),14});
    acquisition2D.totalDurationSec=.25f;acquisition2D.samplingRateHz=5000;
    acquisition2D.initBuffer();triggerSingleShot(gl,state);acquisition2D.isRecording=true;
    modelStyle2D=4;waveStyle2D=1;showComponent2D=2;colorScale2D=35;uiGridOpacity2D=.08f;uiGridSpacing2D=20;
    recordViewer2D.isOpen=false;
    recordViewer2D.documentationCapture=true;
}
// Stage 0 shows raw data, stage 1 AGC, stage 2 a single Vz trace, and stage 3 the Vx section.
void selectDocumentationView2D(int stage) {
    auto& viewer=Legacy2D::recordViewer2D;
    viewer.agc.enabled=stage>0;viewer.viewMode=stage==2?1:0;viewer.component=stage==3?1:0;
    viewer.agc.windowMs=20;viewer.agc.maxBoost=30;viewer.agc.noiseFloorPercent=.002f;
    viewer.showAllTraces=false;viewer.selectedTrace=50;viewer.visibleTraces=60;viewer.firstTrace=20;viewer.gain=stage==2?1.0f:1.5f;viewer.fitRequest=true;
}
void checkExternalBoundaryView2D(bool visible) {
    using namespace Legacy2D;
    const int pad=domainPad2D();
    if(showBoundary2D!=visible||gpuData2D.NX!=simulationContext2D.NX+2*pad||gpuData2D.NZ!=simulationContext2D.NZ+2*pad)
        throw std::runtime_error("B-key external-domain visibility or GPU extent is incorrect.");
    if(simulationContext2D.NX!=gridWidth2D||simulationContext2D.NZ!=gridHeight2D)
        throw std::runtime_error("Absorber consumed user model cells.");
    static const float* fields=nullptr;
    if(visible) fields=gpuData2D.d_vz_1;
    else if(fields&&fields!=gpuData2D.d_vz_1) throw std::runtime_error("B-key rebuilt physical fields.");
    if(visible) std::puts("PASS: B key exposes real external absorbing cells without resizing the physical model.");
}
// Validate local sample indexing when acquisition starts after hundreds of solver time steps.
void checkRecordingTimeline2D(SimState& state,GLHandles& gl) {
    using namespace Legacy2D;
    const float gain=recordViewer2D.gain;
    const auto settings=recordViewer2D.agc;
    // Exercise picking and zoom with the same canvas-local mapping used by docked UI events.
    auto& io=ImGui::GetIO();const auto mouse=io.MousePos;const float wheel=io.MouseWheel;
    acquisition2D.isRecording=false;canvasHovered2D=true;viewOffset2D=ImVec2(0,0);viewZoom2D=1;
    const float aspect=canvasSize2D.x/canvasSize2D.y,modelAspect=float(viewWidth2D())/viewHeight2D();
    const float ax=std::max(1.0f,aspect/modelAspect),ay=std::max(1.0f,modelAspect/aspect);
    io.MousePos=ImVec2(canvasOrigin2D.x+((2*96.25f/viewWidth2D()-1)/ax*.5f+.5f)*canvasSize2D.x,
        canvasOrigin2D.y+(.5f-(1-2*48.25f/viewHeight2D())/ay*.5f)*canvasSize2D.y);
    io.MouseWheel=0;handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(!hoverValid2D||hoverMx2D!=96||hoverMy2D!=48) throw std::runtime_error("Docked canvas picking differs from the displayed grid.");
    const auto brush=state.brushType;const auto radius=state.brushRadius;const bool mouseDown=io.MouseDown[0];
    const int cell=48*simulationContext2D.NX+96,mirror=(simulationContext2D.NZ-1-48)*simulationContext2D.NX+96;
    const std::array<float,4> material={simulationContext2D.lambda[cell],simulationContext2D.lambda2mu[cell],simulationContext2D.mu[cell],simulationContext2D.rho[cell]};
    const float mirrorDensity=simulationContext2D.rho[mirror];
    state.brushType=TOOL_WALL;state.brushRadius=0;io.MouseDown[0]=true;
    handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(simulationContext2D.rho[cell]!=editDensity2D||simulationContext2D.rho[mirror]!=mirrorDensity)
        throw std::runtime_error("Docked brush edits the vertically mirrored grid cell.");
    simulationContext2D.lambda[cell]=material[0];simulationContext2D.lambda2mu[cell]=material[1];
    simulationContext2D.mu[cell]=material[2];simulationContext2D.rho[cell]=material[3];uploadMaterialToGpu();
    state.brushType=brush;state.brushRadius=radius;io.MouseDown[0]=mouseDown;
    acquisition2D.isRecording=true;io.MouseWheel=1;handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(viewZoom2D<=1) throw std::runtime_error("Canvas zoom is blocked during acquisition.");
    resetViewportRequested2D=true;io.MouseWheel=0;handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(viewZoom2D!=1) throw std::runtime_error("Docked canvas view reset failed.");
    io.MousePos=mouse;io.MouseWheel=wheel;canvasHovered2D=false;
    // Check coordinate picking with both physical-only and expanded-boundary views.
    acquisition2D.isRecording=false;
    for(bool boundary:{false,true}) {
        showBoundary2D=boundary;viewZoom2D=1;viewOffset2D=ImVec2(0,0);canvasHovered2D=true;
        const float ratio=float(viewWidth2D())/viewHeight2D();
        const float aspect=canvasSize2D.x/canvasSize2D.y;
        const float ax=std::max(1.0f,aspect/ratio),ay=std::max(1.0f,ratio/aspect);
        io.MousePos=ImVec2(canvasOrigin2D.x+(((96.25f-viewOrigin2D())/viewWidth2D()*2-1)/ax*.5f+.5f)*canvasSize2D.x,
            canvasOrigin2D.y+(.5f-(1-2*(48.25f-viewOriginZ2D())/viewHeight2D())/ay*.5f)*canvasSize2D.y);
        handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
        if(!hoverValid2D||hoverMx2D!=96||hoverMy2D!=48) throw std::runtime_error("Expanded-boundary picking shifted physical coordinates.");
    }
    // A held source button never resets time; a pan button suppresses source events.
    const auto savedSources=activeSources2D;const bool savedMulti=multiSourceMode2D,savedRunning=state.running;
    const int savedX=editSrcX2D,savedZ=editSrcZ2D,sourceStep=currentIt2D;
    const auto savedBrush=state.brushType;const bool left=io.MouseDown[0],right=io.MouseDown[1];
    multiSourceMode2D=true;state.brushType=TOOL_NONE;canvasHovered2D=true;
    io.MouseDown[0]=true;io.MouseDown[1]=true;lastInjectTime2D=-1;
    handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(activeSources2D.size()!=savedSources.size()||currentIt2D!=sourceStep) throw std::runtime_error("Panning triggered a source or reset the field.");
    io.MouseDown[1]=false;lastInjectTime2D=-1;
    handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(activeSources2D.size()!=savedSources.size()+1||currentIt2D!=sourceStep) throw std::runtime_error("Held-left source injection failed or reset time.");
    const auto oldDelta=io.MouseDelta;const float oldDrag=io.MouseDragMaxDistanceSqr[1];
    io.MouseDown[1]=true;io.MouseDelta=ImVec2(12,-8);io.MouseDragMaxDistanceSqr[1]=100;
    panActive2D=true;canvasHovered2D=false;const auto oldOffset=viewOffset2D;
    handleSeismicInteractions(state,int(canvasSize2D.x),int(canvasSize2D.y),0);
    if(!(viewOffset2D.x<oldOffset.x&&viewOffset2D.y<oldOffset.y)) throw std::runtime_error("Right-button drag did not continue outside the canvas.");
    io.MouseDelta=oldDelta;io.MouseDragMaxDistanceSqr[1]=oldDrag;panActive2D=false;
    activeSources2D=savedSources;multiSourceMode2D=savedMulti;state.running=savedRunning;state.brushType=savedBrush;
    editSrcX2D=savedX;editSrcZ2D=savedZ;io.MouseDown[0]=left;io.MouseDown[1]=right;
    std::puts("PASS: physical/boundary picking, non-resetting held-left firing and pan/source exclusion.");
    showBoundary2D=false;viewOffset2D=ImVec2(0,0);viewZoom2D=1;io.MousePos=mouse;
    canvasHovered2D=false;
    const auto originalLanguage=AppUi::currentLanguage();
    AppUi::setLanguage(AppUi::Language::English,false);const auto englishId=ImHashStr(AppUi::windowLabel(canvasName2D));
    AppUi::setLanguage(AppUi::Language::Chinese,false);const auto chineseId=ImHashStr(AppUi::windowLabel(canvasName2D));
    AppUi::setLanguage(originalLanguage,false);
    if(englishId!=chineseId||!ImGui::FindWindowByID(englishId)) throw std::runtime_error("Language changed the 2D dock window identity.");
    const float draftFrequency=sourceFrequency2D;sourceFrequency2D=appliedSourceFrequency2D*1.5f;
    acquisition2D.initBuffer();sourceFrequency2D=draftFrequency;
    if(acquisition2D.frozenModel.frequency!=appliedSourceFrequency2D)
        throw std::runtime_error("Unapplied source edits changed archived shot parameters.");
    acquisition2D.isRecording=true;state.running=true;
    const int firstStep=currentIt2D;
    updateWavefieldSimulation(state);
    const int expected=(currentIt2D-firstStep+acquisition2D.samplingStepStride-1)/acquisition2D.samplingStepStride;
    acquisition2D.isRecording=false;state.running=false;
    if(firstStep<=0||acquisition2D.capturedSamples!=expected||expected<=0)
        throw std::runtime_error("Continuation capture did not start at local sample zero.");
    if(recordViewer2D.records[0].traces.empty()||recordViewer2D.records[0].traces[0].size()!=500)
        throw std::runtime_error("Continuation capture changed the frozen viewer record.");
    const int capturedX=acquisition2D.capturedSourceX;
    ++editSrcX2D;triggerSingleShot(gl,state);state.running=false;
    if(acquisition2D.capturedSourceX!=capturedX||acquisition2D.capturedSamples!=expected)
        throw std::runtime_error("A later source trigger overwrote prior capture geometry or samples.");
    if(recordViewer2D.gain!=gain||!(recordViewer2D.agc==settings)) throw std::runtime_error("Workspace changes reset display parameters.");
    std::puts("PASS: docked canvas picking/brush depth, acquisition-time zoom/reset, bilingual IDs, applied source metadata and viewer gain preservation.");
    std::puts("PASS: 2D continuation capture starts at sample zero; frozen viewer samples and prior shot geometry retained.");
}
// Use deterministic real-data workspaces for the extended documentation sequence.
void configureDocumentationGroups2D(int stage) {
    using namespace Legacy2D;
    static float collapsedHeight=0;
    if(stage==0||stage==2) {
        for(const char* name:{controlsName2D,"Current Parameters###Seis2D.Parameters"}) {
            auto* window=ImGui::FindWindowByID(ImHashStr(AppUi::windowLabel(name)));
            if(!window) throw std::runtime_error("Parameter panel missing during fold validation");
            const auto key=ImHashStr("Seis2D.ParameterCommand",0,window->ID);
            window->StateStorage.SetInt(key,stage==2?2:1);
            window->StateStorage.SetInt(key+1,window->StateStorage.GetInt(key+1)+1);
        }
        focusControls2D=true;
    } else {
        const auto* window=ImGui::FindWindowByID(ImHashStr(AppUi::windowLabel(controlsName2D)));
        if(stage==1) collapsedHeight=window->ContentSize.y;
        if(stage==3) {
            if(window->ContentSize.y<=collapsedHeight+300) throw std::runtime_error("2D grouped fold/expand did not change parameter content");
            std::puts("PASS: 2D grouped parameters collapse/expand across docked panel tabs.");
        }
    }
}
void configureDocumentationWorkspace2D(int stage) {
    using namespace Legacy2D;
    acquisition2D.isRecording=false;recordViewer2D.documentationCapture=false;
    if(stage==0) {
        std::strcpy(exportDirectory2D,"checks/paper-seis2d");
        std::strcpy(exportPrefix2D,"paper_shot");beginExportSession2D(false);exportCapturePackage2D(false);
    }
    dockPreset2D=stage==1?2:stage==2?0:1;resetDockLayout2D=true;
    if(stage==1) recordViewer2D.focusRequest=true;
    if(stage==2) {
        autoAcquisition2D.totalShots=3;autoAcquisition2D.startX=51;autoAcquisition2D.shotIntervalX=60;
        selectExportTab2D=true;focusControls2D=true;
    }
    if(stage==3) {selectModelIoTab2D=true;focusControls2D=true;}
}
}
