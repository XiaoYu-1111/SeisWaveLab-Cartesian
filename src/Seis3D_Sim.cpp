#include "Seis3D/Internal3D.h"
#include "Seis3D_Sim.h"
#include "Cuda_Seis3D.cuh"
#include "ResourcePaths.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Seis3D {
namespace Detail3D {



// Decode UI UTF-8 paths using C++20 char8_t rather than the deprecated u8path overload.
std::filesystem::path uiPath3D(const char* text) {
    std::u8string encoded(std::strlen(text),u8'\0');
    std::memcpy(encoded.data(),text,encoded.size());
    return std::filesystem::path(encoded);
}

// Report CUDA errors through the 3-D panel, preserving the application and 2-D route.
void requireCuda3D(cudaError_t result, const char* operation) {
    if (result != cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(result));
}

// Return a unique, filesystem-safe session suffix without depending on a locale-specific date.
std::string sessionSuffix3D() {
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

// Fit editable survey defaults to a newly selected physical grid.
void fitSurvey3D(Seis3D_State& s) {
    auto& survey=s.survey3D; const auto& c=s.context3D;
    survey.receiverCountX3D=(std::clamp)(survey.receiverCountX3D,1,(std::min)(maxReceivers3D,c.nx3D-2));
    survey.receiverCountY3D=(std::clamp)(survey.receiverCountY3D,1,
        (std::min)(maxReceivers3D/survey.receiverCountX3D,c.ny3D-2));
    survey.shotCountX3D=(std::clamp)(survey.shotCountX3D,1,100);
    survey.shotCountY3D=(std::clamp)(survey.shotCountY3D,1,100);
    survey.receiverOrigin3D[2]=0;
    survey.receiverSpacing3D[0]=(std::max)(1,(c.nx3D-3)/(std::max)(1,survey.receiverCountX3D-1));
    survey.receiverSpacing3D[1]=(std::max)(1,(c.ny3D-3)/(std::max)(1,survey.receiverCountY3D-1));
    // Center a single line on the other axis; center multi-line spreads after integer spacing fits.
    survey.receiverOrigin3D[0]=survey.receiverCountX3D==1?c.nx3D/2:
        (c.nx3D-1-(survey.receiverCountX3D-1)*survey.receiverSpacing3D[0])/2;
    survey.receiverOrigin3D[1]=survey.receiverCountY3D==1?c.ny3D/2:
        (c.ny3D-1-(survey.receiverCountY3D-1)*survey.receiverSpacing3D[1])/2;
    survey.shotOrigin3D[0]=c.nx3D/4; survey.shotOrigin3D[1]=c.ny3D/4; survey.shotOrigin3D[2]=(std::max)(2,c.nz3D/8);
    survey.shotSpacing3D[0]=(std::max)(1,(c.nx3D-2-survey.shotOrigin3D[0])/(std::max)(1,survey.shotCountX3D-1));
    survey.shotSpacing3D[1]=(std::max)(1,(c.ny3D-2-survey.shotOrigin3D[1])/(std::max)(1,survey.shotCountY3D-1));
}

// Replace the complete session only after CPU validation, and roll back GPU/GL partial failures.
void initializeSession3D(Seis3D_State& s,bool imported) {
    if(s.recording3D||s.sequence3D) throw std::logic_error("Stop acquisition before rebuilding the 3D session.");
    auto candidate=s.editContext3D;
    const auto path=uiPath3D(s.modelPath3D);
    if(imported) candidate=inspectModel3D(path,candidate);
    else { if(candidate.modelKind3D==ModelKind3D::Imported) candidate.modelKind3D=ModelKind3D::Uniform; validateContext3D(candidate); }
    if(estimateMemory3D(candidate).totalBytes3D>memoryLimit3D) throw std::runtime_error("Grid exceeds the 4 GiB sandbox budget.");
    releaseSandbox3D(s); s.context3D=candidate; s.editContext3D=candidate;
    try {
        initializeGpu3D(s.gpu3D,s.context3D);
        if(imported) importModel3D(s,path);
        initializeGraphics3D(s);
        s.source3D.x3D=candidate.nx3D/2; s.source3D.y3D=candidate.ny3D/2; s.source3D.z3D=(std::max)(2,candidate.nz3D/4);
        s.slice3D[0]=s.source3D.x3D; s.slice3D[1]=s.source3D.y3D; s.slice3D[2]=s.source3D.z3D;
        resetSliceViews3D(s);s.camera3D=VolumeCamera3D{};
        fitSurvey3D(s);configureReceivers3D(s);s.step3D=0;s.viewMaximum3D=0;s.scalePeak3D=0;s.scaleComponent3D=-1;s.hoverValid3D=false;
        s.referenceMaximum3D=0;s.unitScalePeaks3D={};s.scaleUnit3D=s.component3D==4?1:0;
        // Start the live preview near the source, reducing unnecessary travel time on large spreads.
        std::int64_t nearestDistance=std::numeric_limits<std::int64_t>::max();
        for(int i=0;i<int(s.receivers3D.size());++i) {
            const auto& receiver=s.receivers3D[i];
            const auto dx=std::int64_t(receiver.x3D)-s.source3D.x3D,dy=std::int64_t(receiver.y3D)-s.source3D.y3D;
            if(dx*dx+dy*dy<nearestDistance) {nearestDistance=dx*dx+dy*dy;s.selectedReceiver3D=i;}
        }
        s.ready3D=true;s.dirty3D=true;s.message3D="Model and receiver mesh ready. Run: live preview. Fire + capture: exportable 3-C gather.";
    } catch(...) { releaseSandbox3D(s); throw; }
}

// Validate against the live model, leaving unrelated model edits pending until an explicit rebuild.
void applySourceFrequency3D(Seis3D_State& s) {
    if(!s.ready3D) throw std::logic_error("Initialize the 3D model before applying source frequency.");
    if(s.context3D.frequency3D==s.editContext3D.frequency3D) return;
    if(s.recording3D||s.sequence3D) throw std::logic_error("Stop acquisition before changing source frequency.");
    auto candidate=s.context3D;candidate.frequency3D=s.editContext3D.frequency3D;
    validateContext3D(candidate);
    refreshCpmlCoefficients3D(s.gpu3D,candidate);
    s.context3D=candidate;
}

// Reset the physical field and monitor together, applying frequency while preserving material and display settings.
void restartSimulation3D(Seis3D_State& s,bool running) {
    applySourceFrequency3D(s);
    resetGpu3D(s.gpu3D,s.context3D);s.step3D=0;s.viewMaximum3D=0;s.scalePeak3D=0;s.scaleComponent3D=-1;s.running3D=running;s.singleStep3D=false;s.dirty3D=true;
    s.referenceMaximum3D=0;s.unitScalePeaks3D={};s.scaleUnit3D=s.component3D==4?1:0;
    s.liveTrace3D.count3D=s.liveTrace3D.next3D=0;
    s.message3D=running?"New source wavelet started; live receiver monitoring active.":"Wavefield reset. Run to emit a new source wavelet.";
}

// A completed wavelet cannot be resumed; restart only when the configured run has finished.
void toggleSimulation3D(Seis3D_State& s) {
    if(!s.ready3D) return;
    if(!s.running3D&&!s.recording3D&&s.step3D>=std::uint64_t((std::clamp)(s.maximumSteps3D,1,1000000)))
        restartSimulation3D(s,true);
    else s.running3D=!s.running3D;
}

// Execute a bounded batch so the large-grid solver cannot queue unbounded work ahead of the UI.
void advanceSimulation3D(Seis3D_State& s) {
    if(!s.ready3D||(!s.running3D&&!s.singleStep3D)) return;
    requireCuda3D(cudaEventRecord(s.gpu3D.startEvent3D,s.gpu3D.stream3D),"start 3D timer");
    const bool singleStep=s.singleStep3D;
    const int batch=singleStep?1:(std::clamp)(s.stepsPerFrame3D,1,8); s.singleStep3D=false;
    for(int i=0;i<batch;++i) {
        const Source3D& source=s.recording3D?s.gather3D.source3D:s.source3D;
        runStep3D(s.gpu3D,s.context3D,source,s.step3D); ++s.step3D;
        if(s.recording3D&&s.step3D%std::uint64_t(s.gather3D.stride3D)==0) {
            const auto samples=sampleReceivers3D(s.gpu3D,s.context3D);
            const std::size_t count=s.gather3D.receivers3D.size()*3;
            std::copy_n(samples,count,s.gather3D.samples3D.data()+std::size_t(s.gather3D.sampleCount3D)*count);
            ++s.gather3D.sampleCount3D;
            if(s.gather3D.sampleCount3D==s.gather3D.targetSamples3D) {
                s.recording3D=false; s.running3D=false;
                if(s.sequence3D) {
                    exportGather3D(s,s.sequenceDirectory3D);
                    ++s.sequenceIndex3D;
                    if(s.sequenceIndex3D<s.survey3D.shotCountX3D*s.survey3D.shotCountY3D) {
                        positionSequenceShot3D(s); startGather3D(s,s.sequenceIndex3D+1);
                    } else { s.sequence3D=false; s.message3D="3D multi-line shot sequence completed and exported."; }
                } else s.message3D="3D capture complete. Export the three-component shot gather.";
                break;
            }
        }
        if(!s.recording3D&&s.step3D>=std::uint64_t((std::clamp)(s.maximumSteps3D,1,1000000))) { s.running3D=false; break; }
    }
    // Finishing one shot may prepare the next; manual stepping must still leave continuous runs paused.
    if(singleStep) s.running3D=false;
    if(!s.recording3D) appendLiveTrace3D(s);
    requireCuda3D(cudaEventRecord(s.gpu3D.stopEvent3D,s.gpu3D.stream3D),"stop 3D timer");
    requireCuda3D(cudaEventSynchronize(s.gpu3D.stopEvent3D),"finish 3D compute batch");
    requireCuda3D(cudaEventElapsedTime(&s.computeMs3D,s.gpu3D.startEvent3D,s.gpu3D.stopEvent3D),"read 3D compute time");
}

} // namespace

using namespace Detail3D;

// Initialize CPU-only state and default paths next to the deployed program, inside outer out.
Seis3D_State::Seis3D_State() {
    source3D.x3D=context3D.nx3D/2;source3D.y3D=context3D.ny3D/2;source3D.z3D=context3D.nz3D/4;
    const auto directory=getExecutableDirectory()/"data_Seis3D";
    const auto model=pathToUtf8(directory/"model.s3dm"); const auto output=pathToUtf8(directory);
    std::snprintf(modelPath3D,sizeof(modelPath3D),"%s",model.c_str());
    const auto modelOutput=pathToUtf8(directory/"model_export.s3dm");
    std::snprintf(modelExportPath3D,sizeof(modelExportPath3D),"%s",modelOutput.c_str());
    const auto vtiOutput3D=pathToUtf8(directory/"model_export.vti");
    std::snprintf(vtiModelExportPath3D,sizeof(vtiModelExportPath3D),"%s",vtiOutput3D.c_str());
    const auto wavefieldOutput3D=pathToUtf8(directory/"wavefield_snapshot.vti");
    std::snprintf(vtiWavefieldExportPath3D,sizeof(vtiWavefieldExportPath3D),"%s",wavefieldOutput3D.c_str());
    std::snprintf(exportDirectory3D,sizeof(exportDirectory3D),"%s",output.c_str());
    fitSurvey3D(*this);
}

// Release owned resources while the owning application still has its GL context current.
Seis3D_State::~Seis3D_State() { releaseSandbox3D(*this); }

// Unregister CUDA-GL interop before deleting GL storage, then destroy only the 3-D stream/resources.
void releaseSandbox3D(Seis3D_State& s) noexcept {
    if(s.gpu3D.stream3D) cudaStreamSynchronize(s.gpu3D.stream3D);
    auto& g=s.gl3D;
    if(g.stagingBuffer3D||g.vao3D) glFinish(); // Cold switches wait for the last GL reader before reclaiming VRAM.
    if(g.interop3D) cudaGraphicsUnregisterResource(g.interop3D);
    if(g.stagingBuffer3D) glDeleteBuffers(1,&g.stagingBuffer3D);
    if(g.vao3D) glDeleteVertexArrays(1,&g.vao3D);
    if(g.sliceProgram3D) glDeleteProgram(g.sliceProgram3D);
    if(g.volumeProgram3D) glDeleteProgram(g.volumeProgram3D);
    for(int p=0;p<3;++p) {
        if(g.scalarTextures3D[p]) glDeleteTextures(1,&g.scalarTextures3D[p]);
        if(g.displayTextures3D[p]) glDeleteTextures(1,&g.displayTextures3D[p]);
        if(g.framebuffers3D[p]) glDeleteFramebuffers(1,&g.framebuffers3D[p]);
    }
    if(g.volumeTexture3D) glDeleteTextures(1,&g.volumeTexture3D);
    if(g.volumeDisplayTexture3D) glDeleteTextures(1,&g.volumeDisplayTexture3D);
    if(g.volumeFramebuffer3D) glDeleteFramebuffers(1,&g.volumeFramebuffer3D);
    g=GLHandles3D{}; releaseGpu3D(s.gpu3D);
    s.liveTrace3D=LiveTrace3D{};
    s.ready3D=false; s.running3D=false; s.recording3D=false; s.sequence3D=false; s.singleStep3D=false; s.hoverValid3D=false;
}

} // namespace Seis3D
