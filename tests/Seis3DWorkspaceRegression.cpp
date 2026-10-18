#include "Seis3D_Sim.h"
#include "Seis3D/Internal3D.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "AppTheme.h"
#include "AppWidgets.h"
#include "HomeScreen.h"
#include "Cuda_Check.cuh"
#include "ResourcePaths.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

// Fail with a precise diagnostic rather than allowing a blank texture or invalid dock frame to pass.
void require3D(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}

// Read one rendered physical slice texel without relying on visual interpretation of a screenshot.
std::array<unsigned char,4> pixel3D(const Seis3D::Seis3D_State& s,int x,int y) {
    std::array<unsigned char,4> pixel{};
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s.gl3D.framebuffers3D[0]);
    glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    return pixel;
}

// Check the actual textured quad submitted to ImGui, rather than accepting a correct hidden FBO.
ImVec4 visibleSliceRect3D(GLuint texture) {
    for(const auto* list:ImGui::GetDrawData()->CmdLists) for(const auto& command:list->CmdBuffer) {
        if(command.GetTexID()!=ImTextureID(texture)||command.ElemCount<6) continue;
        ImVec4 rect(1e10f,1e10f,-1e10f,-1e10f);
        for(int i=0;i<6;++i) {
            const auto position=list->VtxBuffer[list->IdxBuffer[int(command.IdxOffset)+i]+command.VtxOffset].pos;
            rect.x=(std::min)(rect.x,position.x);rect.y=(std::min)(rect.y,position.y);
            rect.z=(std::max)(rect.z,position.x);rect.w=(std::max)(rect.w,position.y);
        }
        require3D(rect.z>rect.x&&rect.w>rect.y,"Slice has an empty ImGui image rectangle");return rect;
    }
    throw std::runtime_error("Rendered slice texture never reached ImGui draw commands");
}

// Read a point away from crosshairs/grids and prove that the on-screen material is opaque and colored.
void requireVisibleSlice3D(const Seis3D::Seis3D_State& s) {
    const auto rect=visibleSliceRect3D(s.gl3D.displayTextures3D[0]);
    unsigned char actual[4]{};glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glReadPixels(int(rect.x+(rect.z-rect.x)*.13f),899-int(rect.y+(rect.w-rect.y)*.83f),1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    const auto& l=s.gl3D.layout3D;
    const auto expected=pixel3D(s,int(-l.domainOrigin3D[0]+s.context3D.nx3D*.13f),int(-l.domainOrigin3D[2]+s.context3D.nz3D*.83f));
    require3D(expected[3]==255,"Slice offscreen target is transparent");
    for(int channel=0;channel<3;++channel) require3D(std::abs(int(actual[channel])-int(expected[channel]))<14,"ImGui slice differs from its rendered material texture");
}

// Save the actual GL backbuffer for optional visual review without displaying a native window.
void saveFrame(const std::string& name) {
    std::vector<unsigned char> pixels(1280*900*3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,1280,900,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream image(name+".ppm",std::ios::binary);image<<"P6\n1280 900\n255\n";
    for(int row=899;row>=0;--row) image.write(reinterpret_cast<const char*>(pixels.data()+row*1280*3),1280*3);
}

// Exercise long-axis allocation and cell-centered interpolation using actual CUDA/GL resources.
void checkLongAxisVolume3D() {
    Seis3D::Seis3D_State s;
    s.editContext3D.nx3D=600;s.editContext3D.ny3D=24;s.editContext3D.nz3D=20;
    s.editContext3D.pml3D=8;s.editContext3D.freeSurface3D=false;s.editContext3D.modelKind3D=Seis3D::ModelKind3D::Uniform;
    Seis3D::Detail3D::initializeSession3D(s,false);
    s.component3D=0;
    std::vector<float> linear(s.context3D.cells3D);
    for(std::size_t i=0;i<linear.size();++i) linear[i]=float(i%s.context3D.gx3D);
    Seis3D::Detail3D::requireCuda3D(cudaMemcpy(s.gpu3D.field3D[2],linear.data(),linear.size()*sizeof(float),cudaMemcpyHostToDevice),"seed linear volume");
    Seis3D::Detail3D::updateGraphics3D(s);
    const auto& l=s.gl3D.layout3D;
    require3D(l.volumeX3D==256,"Long axis did not use the 256-voxel display cap");
    std::vector<float> volume(std::size_t(l.volumeX3D)*l.volumeY3D*l.volumeZ3D*2);
    glBindTexture(GL_TEXTURE_3D,s.gl3D.volumeTexture3D);glGetTexImage(GL_TEXTURE_3D,0,GL_RG,GL_FLOAT,volume.data());
    for(int x:{0,97,255}) {
        const float expected=(x+.5f)*l.domainSize3D[0]/l.volumeX3D-.5f+l.domainOrigin3D[0]+s.context3D.ox3D;
        require3D(std::abs(volume[2*x]-expected)<1e-3f,"Cell-centered trilinear volume resampling is incorrect");
    }
    Seis3D::Detail3D::restartSimulation3D(s,true);s.stepsPerFrame3D=8;
    Seis3D::Detail3D::advanceSimulation3D(s);Seis3D::Detail3D::updateGraphics3D(s);
    require3D(s.step3D==8&&std::isfinite(s.viewMaximum3D),"Long-axis solver or rendering failed");
    Seis3D::releaseSandbox3D(s);
}

// Read the actual raymarched color, not a CPU approximation of the shader's optical integration.
std::array<unsigned char,4> volumePixel3D(const Seis3D::Seis3D_State& s) {
    std::array<unsigned char,4> pixel{};
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s.gl3D.volumeFramebuffer3D);
    glReadPixels(s.gl3D.volumeWidth3D/2,s.gl3D.volumeHeight3D/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);return pixel;
}

// Compare identical six-cell fronts in real small/large GPU volumes with fixed amplitude settings.
std::array<float,2> checkVolumeOpacity3D(int nodes) {
    Seis3D::Seis3D_State s;s.editContext3D.nx3D=s.editContext3D.ny3D=s.editContext3D.nz3D=nodes;
    s.editContext3D.freeSurface3D=false;s.editContext3D.modelKind3D=Seis3D::ModelKind3D::Uniform;
    Seis3D::Detail3D::initializeSession3D(s,false);
    const auto& c=s.context3D;
    std::vector<float> plane(std::size_t(c.gx3D)*c.gy3D,.05f);
    for(int z=c.oz3D+nodes/2-3;z<c.oz3D+nodes/2+3;++z)
        Seis3D::Detail3D::requireCuda3D(cudaMemcpy(s.gpu3D.field3D[2]+std::size_t(z)*plane.size(),plane.data(),plane.size()*sizeof(float),cudaMemcpyHostToDevice),"seed equal-thickness wavefront");
    s.component3D=3;s.displayMode3D=1;s.modelStyle3D=4;s.waveStyle3D=7;
    s.autoScale3D=false;s.manualScale3D=1;s.gain3D=s.volumeGain3D=1;
    s.volumeOpacity3D=.2f;s.waveOpacity3D=0;s.dirty3D=true;
    Seis3D::Detail3D::updateGraphics3D(s);const auto background=volumePixel3D(s);
    std::array<float,2> contrast{};
    s.waveOpacity3D=1;
    for(int enabled=0;enabled<2;++enabled) {
        s.volumeSizeCompensation3D=enabled!=0;Seis3D::Detail3D::updateGraphics3D(s);
        const auto pixel=volumePixel3D(s);
        for(int channel=0;channel<3;++channel) contrast[enabled]+=float(std::abs(int(pixel[channel])-int(background[channel])));
    }
    const auto sliceBefore=pixel3D(s,c.ox3D+nodes/2,c.oz3D+nodes/2);
    const auto volumeBefore=volumePixel3D(s);s.volumeGain3D=3;
    Seis3D::Detail3D::updateGraphics3D(s);
    require3D(pixel3D(s,c.ox3D+nodes/2,c.oz3D+nodes/2)==sliceBefore,"Volume gain changed the linked slice transfer function");
    require3D(volumePixel3D(s)!=volumeBefore,"Paused volume gain did not repaint the raymarched output");
    Seis3D::releaseSandbox3D(s);
    std::printf("VOLUME %d cubed: uncorrected RGB contrast %.1f / size-consistent %.1f\n",nodes,contrast[0],contrast[1]);
    return contrast;
}

// Snapshot user display settings separately from measured peaks, sampled data and chosen palettes.
std::vector<float> displayParameters3D(const Seis3D::Seis3D_State& s) {
    return {s.gain3D,s.volumeGain3D,s.manualScale3D,float(s.autoScale3D),float(s.holdPeakScale3D),
        s.waveOpacity3D,s.waveThreshold3D,s.waveGamma3D,s.materialBrightness3D,s.modelOpacity3D,s.gridOpacity3D,
        s.volumeOpacity3D,s.volumeThreshold3D,s.volumeSharpness3D,s.volumeSampling3D,float(s.volumeSizeCompensation3D),
        float(s.materialRangeAuto3D),s.manualMaterialRange3D[0],s.manualMaterialRange3D[1],float(s.materialComponent3D),
        float(s.component3D),float(s.displayMode3D),float(s.showVolume3D),float(s.showBoundary3D),float(s.showGrid3D),
        float(s.showCrosshair3D),float(s.displayStride3D),float(s.volumeLook3D),s.boundaryOpacity3D,s.fontScale3D,
        s.traceGain3D,float(s.traceDisplay3D),float(s.traceRow3D),float(s.traceComponent3D),float(s.traceNormalizeEach3D)};
}

// Exercise model/source lifecycle changes with custom gains and known fields through real CUDA/GL.
void checkDisplayPersistence3D() {
    using namespace Seis3D;using namespace Seis3D::Detail3D;
    Seis3D_State s;s.editContext3D.nx3D=33;s.editContext3D.ny3D=31;s.editContext3D.nz3D=29;s.editContext3D.pml3D=8;
    s.editContext3D.modelKind3D=ModelKind3D::Uniform;
    s.autoScale3D=false;s.manualScale3D=.2f;s.gain3D=4.25f;s.volumeGain3D=2.75f;
    s.waveOpacity3D=.63f;s.waveThreshold3D=.004f;s.waveGamma3D=.78f;
    s.materialBrightness3D=.81f;s.modelOpacity3D=.22f;s.gridOpacity3D=.15f;
    s.volumeOpacity3D=3.1f;s.volumeThreshold3D=.016f;s.volumeSharpness3D=1.35f;s.volumeSampling3D=1.6f;
    s.volumeSizeCompensation3D=false;s.materialRangeAuto3D=false;s.manualMaterialRange3D[0]=2200;s.manualMaterialRange3D[1]=4800;
    s.materialComponent3D=6;s.component3D=0;s.displayMode3D=1;s.showBoundary3D=true;s.showGrid3D=s.showCrosshair3D=false;
    s.displayStride3D=3;s.volumeLook3D=2;s.traceGain3D=1.7f;s.traceDisplay3D=2;s.traceRow3D=2;
    const int initialPreset=s.visualPreset3D;
    const auto expected=displayParameters3D(s);
    initializeSession3D(s,false);require3D(displayParameters3D(s)==expected,"Initialize replaced tuned display parameters");
    const auto seed=[&]{
        std::vector<float> values(s.context3D.cells3D,.01f);
        requireCuda3D(cudaMemcpy(s.gpu3D.field3D[2],values.data(),values.size()*sizeof(float),cudaMemcpyHostToDevice),"seed display persistence field");
        s.dirty3D=true;updateGraphics3D(s);
    };
    seed();const auto original=pixel3D(s,10,10);
    s.gain3D=.2f;updateGraphics3D(s);require3D(pixel3D(s,10,10)!=original,"Paused display gain did not affect rendered intensity");
    s.gain3D=4.25f;updateGraphics3D(s);require3D(pixel3D(s,10,10)==original,"Restoring gain did not restore the rendered intensity");
    s.autoScale3D=true;updateGraphics3D(s);const auto automatic=pixel3D(s,10,10);const float reference=displayScale3D(s);
    setAutomaticScale3D(s,false);updateGraphics3D(s);
    require3D(s.manualScale3D==reference&&pixel3D(s,10,10)==automatic,"Automatic-to-manual scale toggle changed brightness");
    s.manualScale3D=.2f;
    for(int preset=0;preset<5;++preset) {
        const float peak=s.scalePeak3D;applyVisualPreset3D(s,preset);updateGraphics3D(s);
        require3D(displayParameters3D(s)==expected&&s.scalePeak3D==peak,"Visual preset replaced tuned parameters or cleared amplitude statistics");
    }
    applyVisualPreset3D(s,initialPreset);updateGraphics3D(s);
    require3D(pixel3D(s,10,10)==original,"Palette round trip changed fixed-scale intensity");
    restartSimulation3D(s,false);
    require3D(displayParameters3D(s)==expected&&s.viewMaximum3D==0&&s.scalePeak3D==0,"Source reset changed parameters or retained stale statistics");
    s.autoScale3D=true;setAutomaticScale3D(s,false);
    require3D(s.manualScale3D==.2f,"Empty wavefield overwrote the previous manual amplitude scale");
    s.editContext3D.nx3D=35;s.editContext3D.ny3D=33;s.editContext3D.nz3D=31;
    s.editContext3D.vp3D=3200;s.editContext3D.frequency3D=25;
    initializeSession3D(s,false);
    require3D(displayParameters3D(s)==expected&&s.scalePeak3D==0,"Model/grid/frequency rebuild replaced display parameters");
    seed();require3D(pixel3D(s,10,10)==original,"Same physical amplitude/gain changed brightness after rebuild");
    const auto modelPath=getExecutableDirectory()/"checks"/("display-persistence-"+sessionSuffix3D()+".s3dm");
    const auto modelName=pathToUtf8(modelPath);std::snprintf(s.modelPath3D,sizeof(s.modelPath3D),"%s",modelName.c_str());
    std::snprintf(s.modelExportPath3D,sizeof(s.modelExportPath3D),"%s",modelName.c_str());
    exportModel3D(s);initializeSession3D(s,true);
    require3D(s.context3D.modelKind3D==ModelKind3D::Imported&&displayParameters3D(s)==expected,"Imported model reset display parameters");
    seed();require3D(pixel3D(s,10,10)==original,"Model import changed fixed-scale intensity");
    for(int boundary=0;boundary<3;++boundary) {
        applyModelPreset3D(s.editContext3D,ModelKind3D::TwoLayer);
        s.editContext3D.freeSurface3D=boundary==0;s.editContext3D.reflecting3D=boundary==2;
        initializeSession3D(s,false);
        require3D(displayParameters3D(s)==expected,"Model preset / boundary rebuild reset display parameters");
        seed();require3D(pixel3D(s,10,10)==original,"Boundary rebuild changed same-amplitude fixed-scale intensity");
    }
    const int nx=s.editContext3D.nx3D;s.editContext3D.nx3D=0;bool rejected=false;
    try {initializeSession3D(s,false);} catch(const std::invalid_argument&) {rejected=true;}
    require3D(rejected&&s.ready3D&&displayParameters3D(s)==expected,"Rejected model edit changed display parameters or released valid model");
    s.editContext3D.nx3D=nx;s.targetSamples3D=16;s.samplingStride3D=2;s.stepsPerFrame3D=8;s.source3D.amplitude3D*=.5f;
    startGather3D(s,1);for(int i=0;i<4;++i) advanceSimulation3D(s);
    require3D(s.gather3D.sampleCount3D==16&&displayParameters3D(s)==expected,"Capture/source amplitude edit reset display parameters");
    const auto samples=s.gather3D.samples3D;const auto step=s.step3D;
    resetDisplayParameters3D(s);
    require3D(s.gain3D==1.8f&&s.volumeGain3D==1.8f&&s.autoScale3D&&s.gather3D.samples3D==samples&&s.step3D==step,
        "Explicit display reset failed or modified physical samples/time");
    releaseSandbox3D(s);
}

// Verify frequency applies to both the live source and CPML while retaining unrelated edits and frozen shots.
void checkFrequencyApplication3D() {
    using namespace Seis3D;using namespace Seis3D::Detail3D;
    Seis3D_State s;s.editContext3D.nx3D=s.editContext3D.ny3D=s.editContext3D.nz3D=32;
    s.editContext3D.pml3D=8;s.editContext3D.freeSurface3D=false;
    s.survey3D.receiverCountX3D=10;s.survey3D.receiverCountY3D=3;
    initializeSession3D(s,false);s.targetSamples3D=4;s.stepsPerFrame3D=8;
    startGather3D(s,1);advanceSimulation3D(s);
    require3D(s.gather3D.sampleCount3D==4,"Frequency fixture capture incomplete");
    const auto frozen=s.gather3D.samples3D;const float oldFrequency=s.gather3D.context3D.frequency3D;
    auto* material=s.gpu3D.inverseDensity3D;auto* receivers=s.gpu3D.receivers3D;
    const float dt=s.context3D.dt3D;const int receiverCount=s.gpu3D.receiverCount3D;
    s.gain3D=3.25f;s.volumeGain3D=2.4f;s.traceGain3D=1.7f;
    const auto display=displayParameters3D(s);
    const auto readProfile=[&](int axis){
        const int n=axis==0?s.context3D.gx3D:axis==1?s.context3D.gy3D:s.context3D.gz3D;
        std::vector<float> values(n*6);
        requireCuda3D(cudaMemcpy(values.data(),s.gpu3D.coefficients3D[axis],values.size()*sizeof(float),cudaMemcpyDeviceToHost),"read frequency profile");
        return values;
    };
    const auto oldProfile=readProfile(0);
    s.source3D.x3D=17;s.source3D.y3D=9;s.source3D.z3D=11;
    s.editContext3D.nx3D=200;s.editContext3D.vp3D=3500; // Unrelated drafts must stay unapplied.
    s.editContext3D.frequency3D=50;restartSimulation3D(s,false);
    require3D(s.context3D.frequency3D==50&&s.context3D.nx3D==32&&s.context3D.vp3D==3000&&s.context3D.dt3D==dt,
        "Frequency apply changed the grid, material or CFL step");
    require3D(s.gpu3D.inverseDensity3D==material&&s.gpu3D.receivers3D==receivers&&s.gpu3D.receiverCount3D==receiverCount
        &&s.source3D.x3D==17&&s.source3D.y3D==9&&s.source3D.z3D==11&&displayParameters3D(s)==display,
        "Frequency apply replaced model, geometry, source position or tuned display settings");
    require3D(s.gather3D.samples3D==frozen&&s.gather3D.context3D.frequency3D==oldFrequency,"Frequency apply changed the prior frozen shot");
    require3D(readProfile(0)!=oldProfile,"CPML profiles did not change with source frequency");
    GPUSimData3D reference;initializeGpu3D(reference,s.context3D);
    struct Release {GPUSimData3D& g;~Release(){releaseGpu3D(g);}} release{reference};
    for(int axis=0;axis<3;++axis) {
        const auto current=readProfile(axis);std::vector<float> expected(current.size());
        requireCuda3D(cudaMemcpy(expected.data(),reference.coefficients3D[axis],expected.size()*sizeof(float),cudaMemcpyDeviceToHost),"read reference profiles");
        require3D(current==expected,"Updated CPML differs from a fresh initialization at the same frequency");
    }
    auto* profile=s.gpu3D.coefficients3D[0];s.editContext3D.frequency3D=0;bool rejected=false;
    try {restartSimulation3D(s,false);}catch(const std::invalid_argument&) {rejected=true;}
    require3D(rejected&&s.context3D.frequency3D==50&&s.gpu3D.coefficients3D[0]==profile,"Invalid frequency changed active coefficients");
    s.editContext3D.frequency3D=30;startGather3D(s,2);
    require3D(s.context3D.frequency3D==30&&s.gather3D.context3D.frequency3D==30&&displayParameters3D(s)==display,
        "Capture froze a stale frequency or reset display gain");
    s.editContext3D.frequency3D=40;rejected=false;
    try {applySourceFrequency3D(s);}catch(const std::logic_error&) {rejected=true;}
    require3D(rejected&&s.context3D.frequency3D==30,"Frequency changed during an active shot");
    s.editContext3D.frequency3D=30;s.recording3D=false;s.sequence3D=true;
    startGather3D(s,3);require3D(s.recording3D&&s.gather3D.shotId3D==3,"Unchanged frequency blocked the next spread shot");
    std::puts("PASS: frequency/CPML apply, fresh-profile equivalence, draft isolation, source/gain/frozen-shot retention and capture locking.");
}

// Verify weak arrivals become visible without altering the recorded samples or creating signal from silence.
void checkTraceAgc3D() {
    using namespace Seis3D;using namespace Seis3D::Detail3D;
    std::vector<double> times(1000),values(1000,0);
    for(int i=0;i<1000;++i) times[i]=i*.001;
    for(int i=90;i<110;++i) values[i]=1;
    for(int i=790;i<810;++i) values[i]=.002;
    TraceAgcSettings3D settings;settings.enabled3D=true;settings.windowMs3D=50;
    const auto original=values;
    compensateTraceAgc3D(values,times,settings);
    require3D(values[100]==1&&values[800]>.02&&values[800]<=original[800]*settings.maxBoost3D*1.001,
        "Trace AGC failed to boost weak arrivals within the configured cap");
    settings.strength3D=0;values=original;compensateTraceAgc3D(values,times,settings);
    require3D(values==original,"Zero-strength AGC changed the waveform");
    settings.strength3D=1;std::fill(values.begin(),values.end(),0);compensateTraceAgc3D(values,times,settings);
    require3D(std::all_of(values.begin(),values.end(),[](double sample){return sample==0;}),"AGC invented energy in a silent record");
    Seis3D_State s;s.gather3D.receivers3D={{0,0,0}};s.gather3D.sampleCount3D=1000;s.gather3D.sampleIntervalUs3D=1000;
    s.gather3D.samples3D.resize(3000);
    for(int i=0;i<1000;++i) s.gather3D.samples3D[i*3+2]=float(original[i]);
    const auto frozen=s.gather3D.samples3D;s.traceAgc3D=settings;
    const auto enhanced=buildTraceSeries3D(s,true,0,2);
    require3D(enhanced.rawPeak3D==1&&enhanced.values3D[800]>.02&&s.gather3D.samples3D==frozen,
        "Receiver preview AGC modified original gather or failed to expose weak samples");
    const auto cached=buildTraceSeries3D(s,true,0,2);
    require3D(cached.values3D==enhanced.values3D&&s.tracePreviewCache3D.entries3D.size()==1,
        "Frozen receiver preview was not reused");
    s.traceAgc3D.maxBoost3D=5;
    const auto limited=buildTraceSeries3D(s,true,0,2);
    require3D(limited.values3D[800]<enhanced.values3D[800]&&limited.values3D[800]<=.01001,
        "AGC parameter edit did not invalidate frozen preview cache");
    s.traceAgc3D.enabled3D=false;
    const auto unchanged=buildTraceSeries3D(s,true,0,2);
    require3D(std::abs(unchanged.values3D[800]-.002)<1e-9&&s.gather3D.samples3D==frozen,
        "Disabling AGC failed to restore raw preview");
}

// Check row geometry, frozen capture isolation and extrema-preserving/live-ring time sampling.
void checkReceiverRows3D() {
    using namespace Seis3D;using namespace Seis3D::Detail3D;
    std::vector<Receiver3D> grid;for(int y=0;y<5;++y) for(int x=0;x<20;++x) grid.push_back({x*2,y*3,0});
    auto row=selectReceiverRow3D(grid,2,3);
    require3D(row.rowCount3D==5&&row.indices3D.size()==20&&row.indices3D.front()==60&&row.indices3D.back()==79,"20 x 5 X row selection failed");
    row=selectReceiverRow3D(grid,3,7);
    require3D(row.rowCount3D==20&&row.indices3D.size()==5&&row.indices3D.front()==7&&row.indices3D.back()==87,"20 x 5 Y row selection failed");
    require3D(selectReceiverRow3D(grid,1,0).indices3D.size()==100&&selectReceiverRow3D(grid,0,0,55).indices3D.front()==55,"All/single receiver selection failed");
    require3D(selectReceiverRow3D({},2,0).indices3D.empty()&&selectReceiverRow3D(grid,2,999).row3D==4,"Empty/clamped receiver row failed");
    std::reverse(grid.begin(),grid.end());row=selectReceiverRow3D(grid,2,3);
    require3D(grid[row.indices3D.front()].x3D==0&&grid[row.indices3D.back()].x3D==38,"Receiver row was not spatially sorted");
    Seis3D_State s;s.gather3D.receivers3D={{0,0,0},{2,0,0},{0,3,0},{2,3,0}};
    s.gather3D.sampleCount3D=3000;s.gather3D.sampleIntervalUs3D=1000;s.gather3D.firstSampleTime3D=.0025;
    s.gather3D.samples3D.resize(3000*4*3);s.gather3D.samples3D[(1111*4+2)*3+2]=7;s.gather3D.samples3D[(1112*4+2)*3+2]=-5;
    s.survey3D.receiverCountX3D=100;s.survey3D.receiverCountY3D=1;
    const auto trace=buildTraceSeries3D(s,true,2,2);
    require3D(trace.values3D.size()<=2048&&trace.peak3D==7&&std::find(trace.values3D.begin(),trace.values3D.end(),7)!=trace.values3D.end()
        &&std::find(trace.values3D.begin(),trace.values3D.end(),-5)!=trace.values3D.end(),"Preview decimation lost narrow arrivals");
    require3D(trace.times3D.front()==.0025&&std::abs(trace.times3D.back()-3.0015)<1e-10&&std::is_sorted(trace.times3D.begin(),trace.times3D.end()),"Capture preview lost chronological sample times");
    row=selectReceiverRow3D(s.gather3D.receivers3D,2,1);
    require3D(row.indices3D.size()==2&&row.indices3D.front()==2,"Pending survey edit changed frozen gather row selection");
    constexpr int capacity=LiveTrace3D::capacity3D;
    auto& live=s.liveTrace3D;live.receiverCount3D=1;live.count3D=capacity;live.next3D=7;live.samples3D.resize(capacity*3);
    for(int i=0;i<capacity;++i) {const int r=(7+i)%capacity;live.times3D[r]=i*.01;live.samples3D[r*3]=float(i);}
    const auto ring=buildTraceSeries3D(s,false,0,0);
    require3D(ring.values3D.front()==0&&ring.values3D.back()==capacity-1&&std::is_sorted(ring.times3D.begin(),ring.times3D.end()),"Live receiver ring was plotted out of time order");
}

// Prove that component round trips retain a common velocity reference, including decayed fields.
void checkComponentScale3D() {
    using namespace Seis3D;using namespace Seis3D::Detail3D;
    Seis3D_State s;s.editContext3D.nx3D=s.editContext3D.ny3D=s.editContext3D.nz3D=32;s.editContext3D.pml3D=8;
    s.editContext3D.modelKind3D=ModelKind3D::Uniform;initializeSession3D(s,false);
    s.component3D=1;s.displayMode3D=1;s.autoScale3D=true;s.holdPeakScale3D=true;s.gain3D=1.3f;s.volumeGain3D=2.1f;
    const auto seed=[&](float x,float y){
        std::vector<float> values(s.context3D.cells3D,x);requireCuda3D(cudaMemcpy(s.gpu3D.field3D[0],values.data(),values.size()*4,cudaMemcpyHostToDevice),"seed Vx reference");
        std::fill(values.begin(),values.end(),y);requireCuda3D(cudaMemcpy(s.gpu3D.field3D[1],values.data(),values.size()*4,cudaMemcpyHostToDevice),"seed Vy reference");
        s.dirty3D=true;updateGraphics3D(s);
    };
    seed(3,4);require3D(std::abs(displayScale3D(s)-5)<1e-5&&std::abs(s.viewMaximum3D-3)<1e-5,"Vx was normalized by its own maximum instead of |V|");
    selectWaveComponent3D(s,2);updateGraphics3D(s);
    require3D(std::abs(displayScale3D(s)-5)<1e-5&&std::abs(s.viewMaximum3D-4)<1e-5,"Velocity component switch changed common scale");
    seed(.03f,.04f);selectWaveComponent3D(s,1);updateGraphics3D(s);const auto vx=pixel3D(s,12,12);
    for(int component:{2,0,3,1}) {selectWaveComponent3D(s,component);updateGraphics3D(s);require3D(std::abs(displayScale3D(s)-5)<1e-5,"Component switch cleared the held velocity peak");}
    require3D(pixel3D(s,12,12)==vx&&s.gain3D==1.3f&&s.volumeGain3D==2.1f,"Vx round trip changed pixels or gains");
    selectWaveComponent3D(s,4);updateGraphics3D(s);require3D(s.scalePeak3D<1e-6,"Pressure reused a velocity-unit peak");
    selectWaveComponent3D(s,1);updateGraphics3D(s);require3D(displayScale3D(s)>4.99f&&pixel3D(s,12,12)==vx,"Pressure round trip lost the velocity reference");
    setAutomaticScale3D(s,false);require3D(s.manualScale3D>4.99f,"Lock did not use the shared velocity reference");
    selectWaveComponent3D(s,4);s.manualScale3D=200;updateGraphics3D(s);
    selectWaveComponent3D(s,1);require3D(s.manualScale3D>4.99f,"Pressure manual scale replaced velocity scale");
    selectWaveComponent3D(s,4);require3D(s.manualScale3D==200,"Pressure manual scale was not retained");
    selectWaveComponent3D(s,1);restartSimulation3D(s,false);
    require3D(s.scalePeak3D==0&&s.unitScalePeaks3D[1]==0&&s.manualScale3D>4.99f,"New wavefield kept stale unit peaks or lost manual scale");
}

// Write tiny deterministic network-order records to cover all supported sample representations.
void writeShotFixture3D(const std::filesystem::path& path,int format,int traces=2,bool extended=false) {
    const int width=format==3?2:format==8?1:4,begin=3600+(extended?3200:0),record=240+4*width;
    std::vector<unsigned char> bytes(std::size_t(begin)+std::size_t(traces)*record);
    const auto u16=[&](std::size_t at,int v){bytes[at]=static_cast<unsigned char>(unsigned(v)>>8);bytes[at+1]=static_cast<unsigned char>(v);};
    const auto u32=[&](std::size_t at,std::uint32_t v){for(int i=0;i<4;++i) bytes[at+i]=static_cast<unsigned char>(v>>(24-i*8));};
    u16(3216,1000);u16(3220,4);u16(3224,format);u16(3254,1);u16(3500,256);u16(3504,extended?1:0);
    for(int r=0;r<traces;++r) {
        const std::size_t at=begin+std::size_t(r)*record;u32(at+8,7);u32(at+12,r+11);u16(at+28,1);
        u16(at+70,-100);u16(at+88,1);u32(at+80,123450+r*100);u32(at+84,200000);
        u16(at+108,100+r*100);u16(at+114,4);u16(at+116,1000);u16(at+202,1);u16(at+214,-10);
        const int values[4]={1,-2,3,0};const std::uint32_t ibm[4]={0x41100000,0xc1200000,0x41300000,0};
        for(int i=0;i<4;++i) {
            const auto p=at+240+i*width;
            if(format==1) u32(p,ibm[i]);
            else if(format==5) {const float value=float(values[i]);std::uint32_t bits;std::memcpy(&bits,&value,4);u32(p,bits);}
            else if(format==2) u32(p,std::uint32_t(values[i]));
            else if(format==3) u16(p,values[i]);else bytes[p]=static_cast<unsigned char>(values[i]);
        }
    }
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    require3D(bool(file),"Could not write SEG-Y import fixture");
}

// Verify CPU-only import values, coordinates, delays, budgets and rollback without a GPU session.
void checkShotImport3D() {
    using namespace Seis3D;using namespace Seis3D::Detail3D;
    const auto dir=getExecutableDirectory()/"checks"/("shot-import-"+sessionSuffix3D());std::filesystem::create_directories(dir);
    for(int format:{1,2,3,5,8}) {
        const auto path=dir/("format-"+std::to_string(format)+".sgy");writeShotFixture3D(path,format,2,format==5);
        const auto shot=readShotSegy3D(path);
        require3D(shot.gather3D.samples3D[2]==1&&shot.gather3D.samples3D[5]==1&&shot.gather3D.samples3D[8]==-2,"SEG-Y sample decode mismatch");
        require3D(std::abs(shot.coordinates3D[0][0]-1234.5)<1e-9&&shot.coordinates3D[1][0]==1235.5&&shot.traceIds3D[0]==11,"SEG-Y scalar/world coordinates or channel IDs were lost");
        require3D(std::abs(shot.firstTimes3D[0]-.01)<1e-12&&std::abs(shot.firstTimes3D[1]-.02)<1e-12&&shot.amplitudeUnit3D=="Pa","SEG-Y time scalar or amplitude units were lost");
    }
    Seis3D_State s;s.running3D=s.recording3D=s.sequence3D=true;s.gain3D=3.2f;
    const auto good=dir/"format-5.sgy";require3D(importShotRecord3D(s,good)&&!s.ready3D,"Shot import required a GPU model");
    const auto original=s.importedShot3D.gather3D.samples3D;
    const auto series=buildTraceSeries3D(s,true,1,2,true);
    require3D(std::abs(series.times3D.front()-.02)<1e-12&&series.values3D[1]==-2&&buildTraceSeries3D(s,true,1,0,true).values3D.empty(),"Imported trace times or unavailable-component filtering failed");
    require3D(!importShotRecord3D(s,dir/"missing.sgy")&&s.importedShot3D.gather3D.samples3D==original&&s.running3D&&s.recording3D&&s.sequence3D&&s.gain3D==3.2f,"Failed import stopped simulation or destroyed valid data");
    const auto bad=dir/"bad.sgy";writeShotFixture3D(bad,5);
    {std::fstream file(bad,std::ios::in|std::ios::out|std::ios::binary);const unsigned char id[4]={0,0,0,8};file.seekp(3600+256+8);file.write(reinterpret_cast<const char*>(id),4);}
    require3D(!importShotRecord3D(s,bad)&&s.importedShot3D.gather3D.samples3D==original,"Multiple shots were accepted or replaced valid import");
    writeShotFixture3D(bad,5);std::filesystem::resize_file(bad,std::filesystem::file_size(bad)-1);
    require3D(!importShotRecord3D(s,bad),"Truncated SEG-Y was accepted");
    writeShotFixture3D(bad,5,4097);require3D(!importShotRecord3D(s,bad),"Oversized imported channel count was accepted");
}

// Run both synthetic extraction checks and actual source/acquisition/dock interactions.
int main(int argc,char**) {
    if(!glfwInit()) {std::puts("SKIP: GLFW unavailable");return 77;}
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(1280,900,"Seis3D workspace regression",nullptr,nullptr);
    if(!window) {glfwTerminate();std::puts("SKIP: OpenGL 4.3 unavailable");return 77;}
    glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;
    if(glewInit()!=GLEW_OK) {glfwDestroyWindow(window);glfwTerminate();return 77;}
    unsigned int count=0;int devices[8]{};
    if(cudaGLGetDevices(&count,devices,8,cudaGLDeviceListAll)!=cudaSuccess||!count) {
        glfwDestroyWindow(window);glfwTerminate();std::puts("SKIP: CUDA/GL interop unavailable");return 77;
    }
    cudaSetDevice(devices[0]);
    ImGui::CreateContext();ImPlot::CreateContext();
    ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    ImFontConfig fontConfig;fontConfig.SizePixels=21;
    const auto baseFont=resolveResourcePath("font/consola.ttf");
    if(std::filesystem::exists(baseFont)) ImGui::GetIO().Fonts->AddFontFromFileTTF(pathToUtf8(baseFont).c_str(),21,&fontConfig);
    else ImGui::GetIO().Fonts->AddFontDefault(&fontConfig);
    const auto chineseFont=resolveResourcePath("font/msyh.ttc");
    if(std::filesystem::exists(chineseFont)) {
        ImFontConfig merge;merge.MergeMode=true;merge.PixelSnapH=true;
        ImGui::GetIO().Fonts->AddFontFromFileTTF(pathToUtf8(chineseFont).c_str(),21,&merge,ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }
    ImGui_ImplGlfw_InitForOpenGL(window,true);ImGui_ImplOpenGL3_Init("#version 430");
    int result=0;
    try {
        {
            Seis3D::Seis3D_State initial;
            require3D(initial.component3D==0&&initial.visualPreset3D==4&&initial.waveStyle3D==1
                &&initial.modelStyle3D==4&&!initial.holdPeakScale3D,"Initial Vz / Paper scientific / Coolwarm settings are incorrect");
            const auto& q=initial.survey3D;
            require3D(initial.context3D.nx3D==300&&initial.context3D.ny3D==50&&initial.context3D.nz3D==300
                &&initial.targetSamples3D==1000,"Initial 300/50/300 grid or 1000-sample shot default is incorrect");
            for(const auto dimensions:std::array<std::array<int,3>,2>{{{500,20,500},{200,200,200}}}) {
                auto preset=initial.context3D;preset.nx3D=dimensions[0];preset.ny3D=dimensions[1];preset.nz3D=dimensions[2];
                Seis3D::validateContext3D(preset);
                require3D(Seis3D::estimateMemory3D(preset).totalBytes3D<=Seis3D::memoryLimit3D,"New grid preset exceeds the sandbox budget");
            }
            require3D(q.receiverCountX3D==100&&q.receiverCountY3D==5
                &&q.receiverOrigin3D[0]+99*q.receiverSpacing3D[0]<initial.context3D.nx3D-1
                &&q.receiverOrigin3D[1]+4*q.receiverSpacing3D[1]<initial.context3D.ny3D-1,"Initial 100 x 5 spread does not fit");
            require3D(std::string(initial.modelPath3D)!=initial.modelExportPath3D,"Model input/output paths are coupled");
            Seis3D::Detail3D::initializeSession3D(initial,false);
            require3D(initial.receivers3D.size()==500&&initial.gpu3D.receiverCount3D==500,"Initial spread did not deploy 500 channels");
            for(int row=0;row<5;++row) {
                const auto selection=Seis3D::Detail3D::selectReceiverRow3D(initial.receivers3D,2,row);
                require3D(selection.indices3D.size()==100&&selection.rowCount3D==5,"Initial spread does not contain five 100-channel rows");
            }
            for(const auto& receiver:initial.receivers3D) require3D(receiver.z3D==0,"Initial receiver is off the surface");
            Seis3D::Detail3D::startGather3D(initial,1);
            require3D(initial.gather3D.targetSamples3D==1000&&initial.gather3D.samples3D.size()==500*1000*3,
                "Initial capture does not reserve a 500-channel, 1000-sample three-component gather");
            initial.recording3D=initial.running3D=false;
            std::puts("PASS: 300/50/300 defaults, 1000 samples, five 100-channel rows, 500-channel GPU sampling and grid preset budgets.");
            initial.survey3D.receiverCountX3D=20;initial.survey3D.receiverCountY3D=25;
            Seis3D::Detail3D::fitSurvey3D(initial);Seis3D::Detail3D::startGather3D(initial,2);
            require3D(initial.gpu3D.receiverCount3D==500&&initial.gather3D.receivers3D.size()==500
                &&initial.gather3D.samples3D.size()==500*1000*3,"500-channel gather allocation failed");
            Seis3D::Detail3D::advanceSimulation3D(initial);
            require3D(initial.gather3D.sampleCount3D==1,"500-channel GPU sampling failed");
            initial.recording3D=initial.running3D=false;
            for(const auto invalid:{"","../escape","bad/name","bad:name","bad\\name","trailing."}) {
                bool rejected=false;try {Seis3D::Detail3D::validatedExportPrefix3D(invalid);}catch(const std::runtime_error&) {rejected=true;}
                require3D(rejected,"Invalid export prefix accepted");
            }
            Seis3D::Detail3D::resetDisplayParameters3D(initial);
            require3D(initial.component3D==3&&!initial.holdPeakScale3D&&initial.waveStyle3D==1,"Display reset disagrees with Paper scientific defaults");
            for(int language=0;language<2;++language) {
                AppUi::setLanguage(AppUi::Language(language),false);
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
                require3D(drawOperationManualScreen(1280,900)==AppScreen::UserManual,"Manual route changed without input");
                ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
            AppUi::setLanguage(AppUi::Language::English,false);
        }
        checkTraceAgc3D();checkReceiverRows3D();checkDisplayPersistence3D();checkFrequencyApplication3D();checkComponentScale3D();checkShotImport3D();
        GpuInfo deviceInfo{};cudaDeviceProp deviceProperties{};cudaGetDeviceProperties(&deviceProperties,devices[0]);
        std::snprintf(deviceInfo.name,sizeof(deviceInfo.name),"%s",deviceProperties.name);
        deviceInfo.success=true;deviceInfo.totalMem=deviceProperties.totalGlobalMem;
        deviceInfo.computeCapabilityMajor=deviceProperties.major;deviceInfo.computeCapabilityMinor=deviceProperties.minor;
        deviceInfo.multiProcessorCount=deviceProperties.multiProcessorCount;
        for(int theme=0;theme<int(AppUi::ThemeId::Count);++theme) {
            AppUi::setTheme(AppUi::ThemeId(theme),false);
            const auto& p=AppUi::palette();
            require3D(std::abs(p.text.x-p.background.x)>.45f,"Theme text lacks contrast");
            const auto original=ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            const int colorDepth=GImGui->ColorStack.Size;
            for(int role=0;role<8;++role) {
                {AppUi::FunctionStyleScope functionColors{AppUi::FunctionRole(role)};
                    const auto expected=AppUi::functionColor(AppUi::FunctionRole(role));
                    require3D(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark).x==expected.x,"Function colors were not applied");}
                require3D(GImGui->ColorStack.Size==colorDepth&&ImGui::GetStyleColorVec4(ImGuiCol_CheckMark).x==original.x,"Function color scope leaked into another panel");
            }
            if(p.light) {
                {AppUi::LegacyContrastScope legacy(true);
                    require3D(ImGui::GetStyleColorVec4(ImGuiCol_Text).x>.8f,"Legacy dark canvas lost readable text");}
                require3D(AppUi::currentTheme()==AppUi::ThemeId(theme)
                    && ImGui::GetStyleColorVec4(ImGuiCol_Text).x==p.text.x,"Legacy scope changed selected theme");
            }
            for(int frame=0;frame<3;++frame) {
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
                require3D(drawHomeScreen(deviceInfo,1280,900)==AppScreen::Intro0,"Home changed route without input");
                ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
            if(argc>1) saveFrame("home-theme-"+std::to_string(theme));
        }
        {AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Source);
            AppUi::setTheme(AppUi::ThemeId::ArcticLab,false);}
        require3D(ImGui::GetStyleColorVec4(ImGuiCol_Text).x==AppUi::palette().text.x,"Theme switch restored stale scope colors");
        {AppUi::LegacyContrastScope colors(true);AppUi::setTheme(AppUi::ThemeId::WarmIvory,false);}
        require3D(ImGui::GetStyleColorVec4(ImGuiCol_Text).x==AppUi::palette().text.x,"Legacy theme switch restored stale colors");
        // Mouse activation verifies that each visible launch card routes to its intended sandbox.
        for(int dimension=0;dimension<2;++dimension) {
            ImGui::GetIO().AddMousePosEvent(dimension==0?350.0f:950.0f,400.0f);
            for(int phase=0;phase<2;++phase) {
                ImGui::GetIO().AddMouseButtonEvent(0,phase==0);
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
                const auto route=drawHomeScreen(deviceInfo,1280,900);
                if(phase==1) require3D(route==(dimension==0?AppScreen::SeisSim_GPU:AppScreen::SeisSim_3D),"Home launch card routed incorrectly");
                ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
        }
        ImGui::GetIO().AddMousePosEvent(-100,-100);
        AppUi::setTheme(AppUi::ThemeId::CudaGreen,false);
        Seis3D::Seis3D_State s;
        s.survey3D.receiverCountX3D=s.survey3D.receiverCountY3D=10;
        s.materialBrightness3D=1;s.component3D=0;
        s.editContext3D.nx3D=33;s.editContext3D.ny3D=31;s.editContext3D.nz3D=29;s.editContext3D.pml3D=8;
        Seis3D::Detail3D::initializeSession3D(s,false);
        Seis3D::Detail3D::configureReceivers3D(s);
        require3D(s.receivers3D.size()==100 && s.gpu3D.receiverCount3D==100,"Explicit 10 x 10 acquisition failed");
        bool rejected=false;
        s.survey3D.receiverCountX3D=Seis3D::maxReceivers3D/10+1;
        try {Seis3D::Detail3D::configureReceivers3D(s);} catch(const std::runtime_error&) {rejected=true;}
        require3D(rejected && s.gpu3D.receiverCount3D==100,"Over-limit geometry replaced valid receivers");
        s.survey3D.receiverCountX3D=10;
        std::vector<Seis3D::Receiver3D> tooMany(Seis3D::maxReceivers3D+1);
        rejected=false;
        try {Seis3D::setReceivers3D(s.gpu3D,s.context3D,tooMany.data(),int(tooMany.size()));} catch(const std::invalid_argument&) {rejected=true;}
        require3D(rejected && s.gpu3D.receiverCount3D==100,"CUDA receiver API accepted more than 500 channels");
        s.displayMode3D=2;
        Seis3D::Detail3D::updateGraphics3D(s);
        require3D(std::abs(s.materialRange3D[0]-3000)<1 && std::abs(s.materialRange3D[1]-4200)<1,"Material extrema / partial warp reduction failed");
        require3D(pixel3D(s,10,4)!=pixel3D(s,10,24),"Layered model has no visible material contrast");
        const auto materialPixel=pixel3D(s,10,4);
        s.displayMode3D=0;Seis3D::Detail3D::updateGraphics3D(s);
        require3D(pixel3D(s,10,4)==materialPixel,"Zero wavefield changed material background");
        s.displayMode3D=1;Seis3D::Detail3D::updateGraphics3D(s);
        require3D(pixel3D(s,10,4)==pixel3D(s,10,24),"Wave-only mode leaked model values");
        std::vector<float> velocity(s.context3D.cells3D,.25f);
        Seis3D::Detail3D::requireCuda3D(cudaMemcpy(s.gpu3D.field3D[2],velocity.data(),velocity.size()*sizeof(float),cudaMemcpyHostToDevice),"seed known display field");
        s.dirty3D=true;s.autoScale3D=false;s.manualScale3D=.25f;s.waveOpacity3D=1;s.waveThreshold3D=0;
        s.waveStyle3D=0;Seis3D::Detail3D::updateGraphics3D(s);const auto red=pixel3D(s,10,4);
        require3D(std::abs(s.viewMaximum3D-.25f)<1e-5f,"Wave/material channels were mixed");
        s.waveStyle3D=13;Seis3D::Detail3D::updateGraphics3D(s);const auto green=pixel3D(s,10,4);
        require3D(red!=green && red[0]>red[1] && green[1]>green[0],"Wave palette switch was not rendered");
        // Visible CPML belongs to the same amplitude scale; late tails must not reset the held peak.
        for(std::size_t i=0;i<velocity.size();++i) if(int(i%s.context3D.gx3D)<s.context3D.ox3D) velocity[i]=2;
        cudaMemcpy(s.gpu3D.field3D[2],velocity.data(),velocity.size()*sizeof(float),cudaMemcpyHostToDevice);
        s.showBoundary3D=true;s.dirty3D=true;s.scalePeak3D=0;s.holdPeakScale3D=true;
        Seis3D::Detail3D::updateGraphics3D(s);
        require3D(std::abs(s.viewMaximum3D-2)<1e-5f,"Visible CPML was excluded from amplitude statistics");
        std::fill(velocity.begin(),velocity.end(),.0025f);
        cudaMemcpy(s.gpu3D.field3D[2],velocity.data(),velocity.size()*sizeof(float),cudaMemcpyHostToDevice);
        s.autoScale3D=true;s.dirty3D=true;Seis3D::Detail3D::updateGraphics3D(s);
        require3D(std::abs(s.viewMaximum3D-.0025f)<1e-6f && Seis3D::displayScale3D(s)>1.99f,"Peak hold amplified the late-time residual");
        s.holdPeakScale3D=false;Seis3D::Detail3D::updateGraphics3D(s);
        require3D(std::abs(Seis3D::displayScale3D(s)-.0025f)<1e-6f,"Instantaneous autoscale is unavailable");
        s.holdPeakScale3D=true;s.showBoundary3D=false;s.dirty3D=true;
        s.volumeRequestedWidth3D=1200;s.volumeRequestedHeight3D=700;
        Seis3D::Detail3D::updateGraphics3D(s);
        glBindTexture(GL_TEXTURE_2D,s.gl3D.volumeDisplayTexture3D);
        GLint outputWidth=0,outputHeight=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&outputWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&outputHeight);
        require3D(outputWidth==1200&&outputHeight==700,"Volume framebuffer did not follow the rectangular viewport");
        for(int property=5;property<=7;++property) {
            s.materialComponent3D=property;s.dirty3D=true;Seis3D::Detail3D::updateGraphics3D(s);
            require3D(std::isfinite(s.materialRange3D[0])&&s.materialRange3D[1]>s.materialRange3D[0],"Vp/Vs/density range failed");
        }
        // Exercise a real shot to populate the trace panel and clear the synthetic display field.
        s.materialComponent3D=5;s.displayMode3D=0;s.autoScale3D=true;s.waveOpacity3D=.9f;
        s.targetSamples3D=64;s.samplingStride3D=2;s.stepsPerFrame3D=8;
        Seis3D::Detail3D::startGather3D(s,1);
        for(int batch=0;batch<16;++batch) Seis3D::Detail3D::advanceSimulation3D(s);
        require3D(s.gather3D.sampleCount3D==64,"Display changes altered receiver acquisition cadence");
        require3D(s.gather3D.receivers3D.size()==100 && s.gather3D.samples3D.size()==100*64*3,"100-channel three-component gather size is wrong");
        for(const auto arrangement:{std::pair<int,int>{20,5},{5,20},{100,1},{1,100}}) {
            s.survey3D.receiverCountX3D=arrangement.first;s.survey3D.receiverCountY3D=arrangement.second;
            Seis3D::Detail3D::fitSurvey3D(s);Seis3D::Detail3D::configureReceivers3D(s);
            const auto row=Seis3D::Detail3D::selectReceiverRow3D(s.receivers3D,2,0);
            require3D(row.indices3D.size()==s.survey3D.receiverCountX3D&&row.rowCount3D==s.survey3D.receiverCountY3D
                &&s.gpu3D.receiverCount3D==int(s.receivers3D.size()),"Deployed receiver arrangement does not match selected row");
            require3D(s.gather3D.receivers3D.size()==100&&s.gather3D.sampleCount3D==64,"Deploying a new arrangement changed the frozen capture");
        }
        s.survey3D.receiverCountX3D=s.survey3D.receiverCountY3D=10;
        Seis3D::Detail3D::fitSurvey3D(s);Seis3D::Detail3D::configureReceivers3D(s);
        for(int preset=0;preset<4;++preset) {
            s.layoutPreset3D=preset;s.resetLayout3D=true;
            AppUi::setTheme(preset==0?AppUi::ThemeId::CudaGreen:preset==1?AppUi::ThemeId::OceanBlue:AppUi::ThemeId::ScientificLight,false);
            for(int frame=0;frame<4;++frame) {
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
                Seis3D::renderScreen3D(s,1280,900);ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                require3D(!s.messageIsError3D,s.message3D.c_str());
            }
            if(preset==0) requireVisibleSlice3D(s);
            if(argc>1) {
                std::vector<unsigned char> pixels(1280*900*3);
                glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
                glReadPixels(0,0,1280,900,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
                std::ofstream image("seis3d-layout-"+std::to_string(preset)+".ppm",std::ios::binary);
                image<<"P6\n1280 900\n255\n";
                for(int row=899;row>=0;--row) image.write(reinterpret_cast<const char*>(pixels.data()+row*1280*3),1280*3);
            }
            for(const char* name:{"Simulation controls###Seis3D.Controls","Inline X-Z###Seis3D.Inline","Spatial volume###Seis3D.Volume"}) {
                const auto* panel=ImGui::FindWindowByName(name);
                require3D(panel && panel->DockId!=0,"Panel did not join its isolated dockspace");
            }
        }
        // Real mouse events cover the new canvas path and prevent zoom from breaking physical picking.
        s.layoutPreset3D=0;s.resetLayout3D=true;
        const auto drawFrame=[&]{
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
            Seis3D::renderScreen3D(s,1280,900);ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        };
        // Exercise catalog formats, stable IDs, real compact panels and locale switching on a paused capture.
        require3D(AppUi::validateLanguageCatalog(),"Translation catalog has duplicate keys or incompatible printf arguments");
        const auto settingsBeforeLanguage=displayParameters3D(s);
        const auto samplesBeforeLanguage=s.gather3D.samples3D;
        const auto stepBeforeLanguage=s.step3D;
        for(int i=0;i<3;++i) drawFrame();
        const auto* controlsBefore=ImGui::FindWindowByName("Simulation controls###Seis3D.Controls");
        const auto dockBefore=controlsBefore->DockId;
        AppUi::setLanguage(AppUi::Language::English,false);
        const auto gainId=ImHashStr(AppUi::label("Display gain")),browseId=ImHashStr(AppUi::label("Browse##Model"));
        require3D(ImHashStr(AppUi::label("Seis3D Dock Host"))==ImHashStr("Seis3D Dock Host"),"Localization changed the saved dockspace host seed");
        auto* oldWindow=ImGui::CreateNewWindowSettings("Model I/O");
        oldWindow->Pos=ImVec2ih(137,241);oldWindow->Size=ImVec2ih(460,720);
        const auto* migrated=ImGui::FindWindowSettingsByID(ImHashStr(AppUi::windowLabel("Model I/O")));
        require3D(migrated&&migrated->Pos.x==137&&migrated->Pos.y==241&&migrated->Size.y==720,"Legacy window settings migration lost placement");
        for(auto language:{AppUi::Language::Chinese,AppUi::Language::English,AppUi::Language::Chinese}) {
            AppUi::setLanguage(language,false);
            for(int i=0;i<3;++i) drawFrame();
            const auto* controls=ImGui::FindWindowByName("Simulation controls###Seis3D.Controls");
            require3D(controls==controlsBefore&&controls->DockId==dockBefore,"Language switch recreated or undocked a window");
            require3D(displayParameters3D(s)==settingsBeforeLanguage&&s.gather3D.samples3D==samplesBeforeLanguage&&s.step3D==stepBeforeLanguage,"Language switch changed display settings, samples or time");
            require3D(ImHashStr(AppUi::label("Display gain"))==gainId&&ImHashStr(AppUi::label("Browse##Model"))==browseId,"Localized widget identities changed");
            const char* entries=AppUi::items("Vx\0Velocity magnitude\0Pressure\0");
            require3D(std::strcmp(entries,"Vx")==0,"Localization reordered packed combo items");
            entries+=std::strlen(entries)+1;
            require3D(std::strcmp(entries,AppUi::text("Velocity magnitude"))==0,"Packed combo translation failed");
        }
        if(argc>1) saveFrame("seis3d-compact-chinese");
        float closedHeight=0,openHeight=0;
        for(int command:{1,2,1}) for(int frame=0;frame<2;++frame) {
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2(470,850));ImGui::Begin("Fold regression");
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("Seis3D.SectionOverride"),command);
            const float start=ImGui::GetCursorPosY();
            Seis3D::Detail3D::drawSimulationControls3D(s);
            const float height=ImGui::GetCursorPosY()-start;
            if(command==1) closedHeight=height;else openHeight=height;
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("Seis3D.SectionOverride"),0);
            ImGui::End();ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        require3D(openHeight>closedHeight+300,"Collapsing parameter groups did not reduce panel height");
        std::printf("PANELS: simulation controls %.0f px collapsed / %.0f px expanded\n",closedHeight,openHeight);
        for(auto language:{AppUi::Language::English,AppUi::Language::Chinese}) {
            AppUi::setLanguage(language,false);
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
            ImGui::Begin("Fold regression");
            require3D(!Seis3D::Detail3D::sectionHeader3D("MODEL / MATERIAL"),"Language switch reopened a folded parameter group");
            ImGui::End();ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        require3D(displayParameters3D(s)==settingsBeforeLanguage&&s.gather3D.samples3D==samplesBeforeLanguage,"Folding changed display settings or captured data");
        AppUi::setLanguage(AppUi::Language::English,false);
        const auto frozenSamples=s.gather3D.samples3D;
        const auto frozenStep=s.step3D;
        const auto exportDir=getExecutableDirectory()/"checks"/("shot-roundtrip-"+Seis3D::Detail3D::sessionSuffix3D());
        Seis3D::Detail3D::exportGather3D(s,exportDir);
        bool collisionRejected=false;
        try {Seis3D::Detail3D::exportGather3D(s,exportDir);}catch(const std::runtime_error&) {collisionRejected=true;}
        require3D(collisionRejected,"Existing gather was overwritten");
        std::snprintf(s.exportPrefix3D,sizeof(s.exportPrefix3D),"%s","中文测试");
        Seis3D::Detail3D::exportGather3D(s,exportDir);
        require3D(std::filesystem::exists(exportDir/Seis3D::Detail3D::uiPath3D("中文测试_0001_vz.sgy"))
            &&std::filesystem::exists(exportDir/Seis3D::Detail3D::uiPath3D("中文测试_0001_geometry.csv")),"Unicode export prefix was not preserved");
        std::snprintf(s.exportPrefix3D,sizeof(s.exportPrefix3D),"shot");
        require3D(Seis3D::Detail3D::importShotRecord3D(s,exportDir/"shot_0001_vx.sgy"),s.message3D.c_str());
        const auto& imported=s.importedShot3D;
        require3D(imported.component3D==0&&imported.amplitudeUnit3D=="m/s"&&imported.gather3D.receivers3D.size()==100,"Own export/import component metadata failed");
        for(int i=0;i<64;++i) for(int r=0;r<100;++r)
            require3D(imported.gather3D.samples3D[(i*100+r)*3]==s.gather3D.samples3D[(i*100+r)*3],"SEG-Y export/import changed velocity samples");
        require3D(std::abs(imported.firstTimes3D.front()-s.gather3D.firstSampleTime3D)<1e-7&&imported.coordinates3D[55][0]==s.receivers3D[55].x3D*s.context3D.spacing3D,"SEG-Y round trip changed geometry or time origin");
        s.layoutPreset3D=3;s.resetLayout3D=true;
        for(int mode=0;mode<4;++mode) {s.traceDisplay3D=mode;s.traceRow3D=4;for(int i=0;i<2;++i) drawFrame();
            require3D(!s.messageIsError3D&&glGetError()==GL_NO_ERROR,"Imported single/row plot failed");}
        if(argc>1) saveFrame("seis3d-imported-shot");
        s.layoutPreset3D=0;s.resetLayout3D=true;
        require3D(s.gather3D.samples3D==frozenSamples&&s.step3D==frozenStep,"Shot import changed captured samples or simulation time");
        for(int mode=1;mode<4;++mode) {
            s.traceView3D=2;s.traceDisplay3D=mode;s.traceRow3D=999;
            s.traceNormalizeEach3D=mode==3;s.tracePositiveFill3D=mode!=1;
            for(int frame=0;frame<2;++frame) drawFrame();
            require3D(!s.messageIsError3D&&glGetError()==GL_NO_ERROR,"Multi-receiver plot failed to render");
            if(mode>=2) require3D(s.traceRow3D==9,"Displayed receiver row was not clamped to actual capture geometry");
            require3D(s.gather3D.samples3D==frozenSamples&&s.step3D==frozenStep,"Receiver display modified recorded samples or simulation time");
        }
        s.traceDisplay3D=2;s.traceRow3D=4;s.traceNormalizeEach3D=false;s.tracePositiveFill3D=true;
        s.layoutPreset3D=3;s.resetLayout3D=true;for(int frame=0;frame<3;++frame) drawFrame();
        if(argc>1) saveFrame("seis3d-receiver-row");
        s.layoutPreset3D=0;s.resetLayout3D=true;s.traceDisplay3D=0;s.traceView3D=0;
        for(int i=0;i<3;++i) drawFrame();
        auto rect=visibleSliceRect3D(s.gl3D.displayTextures3D[0]);
        ImGui::GetIO().AddMousePosEvent(rect.x+(rect.z-rect.x)*.4f,rect.y+(rect.w-rect.y)*.45f);drawFrame();
        const auto beforePoint=s.hoverPoint3D;
        ImGui::GetIO().AddMouseWheelEvent(0,3);drawFrame();
        require3D(s.sliceCameras3D[0].zoom3D>1.5f&&s.sliceCameras3D[1].zoom3D==1,"Wheel zoom did not remain local to its plane");
        require3D(s.hoverPoint3D.x3D==beforePoint.x3D&&s.hoverPoint3D.z3D==beforePoint.z3D,"Pointer-anchored zoom changed the picked cell");
        const float oldCenter=s.sliceCameras3D[0].center3D[0];
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle,true);drawFrame();
        ImGui::GetIO().AddMousePosEvent(rect.x+(rect.z-rect.x)*.5f,rect.y+(rect.w-rect.y)*.45f);drawFrame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle,false);drawFrame();
        require3D(s.sliceCameras3D[0].center3D[0]<oldCenter,"Middle-button panning did not move the view");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_R,true);drawFrame();ImGui::GetIO().AddKeyEvent(ImGuiKey_R,false);drawFrame();
        require3D(s.sliceCameras3D[0].zoom3D==1&&s.sliceCameras3D[0].center3D[0]==.5f,"R did not reset the slice camera");
        ImGui::GetIO().AddMousePosEvent(-100,-100);
        // Ordinary Run produces real monitor samples and completion can replay a fresh wavelet.
        Seis3D::Detail3D::restartSimulation3D(s,true);s.maximumSteps3D=128;
        for(int i=0;i<16;++i) Seis3D::Detail3D::advanceSimulation3D(s);
        require3D(!s.running3D&&s.liveTrace3D.count3D==16,"Run did not populate the live monitor");
        float livePeak=0;for(float value:s.liveTrace3D.samples3D) livePeak=(std::max)(livePeak,std::abs(value));
        require3D(livePeak>1e-10f,"Live receiver monitor has no physical signal");
        Seis3D::Detail3D::toggleSimulation3D(s);
        require3D(s.running3D&&s.step3D==0&&s.liveTrace3D.count3D==0,"Completed run did not restart its source wavelet");
        s.maximumSteps3D=10000;s.stepsPerFrame3D=1;
        constexpr int capacity=Seis3D::LiveTrace3D::capacity3D;
        for(int i=0;i<capacity+8;++i) Seis3D::Detail3D::advanceSimulation3D(s);
        require3D(s.liveTrace3D.count3D==capacity&&s.liveTrace3D.samples3D.size()==capacity*100*3,"Live monitor exceeded its bounded capacity");
        const int last=(s.liveTrace3D.next3D+capacity-1)%capacity;
        require3D(s.liveTrace3D.times3D[last]>s.liveTrace3D.times3D[s.liveTrace3D.next3D],"Wrapped live sample times lost chronological order");
        require3D(s.gather3D.sampleCount3D==64,"Live monitoring overwrote the frozen capture");
        s.running3D=false;
        s.traceView3D=1;s.traceDisplay3D=3;s.traceRow3D=2;drawFrame();
        require3D(!s.messageIsError3D&&glGetError()==GL_NO_ERROR,"Wrapped live receiver row failed to render");
        s.traceView3D=0;s.traceDisplay3D=0;
        for(int preset=0;preset<5;++preset) {
            Seis3D::Detail3D::applyVisualPreset3D(s,preset);drawFrame();
            require3D(!s.messageIsError3D,s.message3D.c_str());
        }
        const auto stepBeforeRecipes=s.step3D;
        const float sliceGainBeforeRecipes=s.gain3D;
        for(int look=0;look<3;++look) {
            Seis3D::Detail3D::applyVolumeLook3D(s,look);drawFrame();
            require3D(!s.messageIsError3D,s.message3D.c_str());
            require3D(s.volumeLook3D==look&&s.volumeSizeCompensation3D,"Volume recipe did not restore its settings");
            require3D(s.step3D==stepBeforeRecipes&&s.gain3D==sliceGainBeforeRecipes,"Volume recipe changed simulation time or slice gain");
        }
        require3D(glGetError()==GL_NO_ERROR,"OpenGL validation error");
        Seis3D::releaseSandbox3D(s);
        require3D(!s.ready3D&&!s.gl3D.interop3D&&!s.gpu3D.stream3D,"Cold release left resources alive");
        checkLongAxisVolume3D();
        const auto small=checkVolumeOpacity3D(96),large=checkVolumeOpacity3D(300);
        require3D(small[0]>10&&large[0]<small[0]*.75f,"Large-grid opacity diagnosis was not reproduced");
        require3D(large[1]>large[0]*1.4f&&large[1]>small[1]*.65f&&large[1]<small[1]*1.5f,"Equal-thickness wavefront contrast was not restored");
        std::puts("PASS: bilingual catalog/IDs/docking/state preservation, compact parameter groups, common velocity reference and unit-safe component round trips, SEG-Y CPU import/formats/rollback/export round trip/UI, display persistence, scale lock pixels, receiver row sections/extrema/live ring, large-volume opacity/gain, volume recipes, functional colors, ten themes, visible-CPML scale, peak hold, on-screen slices, zoom/pan/reset, monitor/replay, 100-channel capture, RG interop, four dock layouts, cold release");
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());result=1;}
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImPlot::DestroyContext();ImGui::DestroyContext();
    glfwDestroyWindow(window);glfwTerminate();return result;
}
