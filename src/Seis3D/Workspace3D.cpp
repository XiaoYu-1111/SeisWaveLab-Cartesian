#include "AppWidgets.h"
#include "AppShortcuts.h"
#include "Internal3D.h"
#include "Visual3D.h"
#include "imgui_internal.h"
#include "AppTheme.h"
#include "Seis3D_Sim.h"
#include "Cuda_Seis3D.cuh"
#include "CpuSolver3D.h"
#include <omp.h>
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

// Place labels above full-width controls so docking to a narrow column never clips their names.
std::string fieldLabel3D(const char* label) {
    AppUi::Widgets::TextUnformatted(label);ImGui::SetNextItemWidth(-1);
    return std::string("##")+label;
}

// Give actions a purpose color and an inset marker while retaining standard keyboard/disabled behavior.
bool functionButton3D(const char* label,AppUi::FunctionRole role,ImVec2 size=ImVec2(0,0)) {
    AppUi::FunctionStyleScope colors(role,true);
    const bool clicked=AppUi::Widgets::Button(label,size);
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x+3,a.y+5),ImVec2(a.x+3,b.y-5),
        AppUi::color(AppUi::functionColor(role),ImGui::GetStyle().Alpha),2);
    return clicked;
}

// Seed the panel command with the window ID; BeginTabItem pushes a different widget ID scope.
ImGuiID sectionCommandKey3D() {
    return ImHashStr("Seis3D.SectionOverride",0,ImGui::GetCurrentWindow()->ID);
}

// Preserve each scoped header's fold state while reading the panel-wide command outside the tab scope.
bool sectionHeader3D(const char* label,ImGuiTreeNodeFlags flags) {
    const int command=ImGui::GetStateStorage()->GetInt(sectionCommandKey3D(),0);
    const auto revisionKey=sectionCommandKey3D()+1;const int revision=ImGui::GetStateStorage()->GetInt(revisionKey);
    const auto seen=ImHashStr(label,0,ImGui::GetID("Seis3D.GroupRevision"));
    if(ImGui::GetStateStorage()->GetInt(seen)!=revision) {
        ImGui::SetNextItemOpen(command==2,ImGuiCond_Always);ImGui::GetStateStorage()->SetInt(seen,revision);
    }
    return AppUi::Widgets::CollapsingHeader(label,flags);
}

// Use section colors as an additional cue; the full text label remains the primary identifier.
bool functionHeader3D(const char* label,AppUi::FunctionRole role,ImGuiTreeNodeFlags flags=ImGuiTreeNodeFlags_None) {
    AppUi::FunctionStyleScope colors(role);
    return sectionHeader3D(label,flags);
}

// Apply expand/collapse only to the active panel during this frame, preserving every parameter.
void drawSectionTools3D() {
    auto* storage=ImGui::GetStateStorage();const auto key=sectionCommandKey3D();
    if(AppUi::Widgets::SmallButton("Expand all")) {storage->SetInt(key,2);storage->SetInt(key+1,storage->GetInt(key+1)+1);}
    ImGui::SameLine();if(AppUi::Widgets::SmallButton("Collapse all")) {storage->SetInt(key,1);storage->SetInt(key+1,storage->GetInt(key+1)+1);}
    ImGui::Separator();
}

// Scope the tab's own draw colors without leaking them into a theme selector in its contents.
bool functionTab3D(const char* label,AppUi::FunctionRole role) {
    AppUi::FunctionStyleScope colors(role);
    return AppUi::Widgets::BeginTabItem(label);
}

// Make pending/active frequency visible and reset only dynamic fields when explicitly applying it.
void drawSourceFrequencyControls3D(Seis3D_State& s) {
    AppUi::Widgets::InputFloat(fieldLabel3D("Ricker f0 (Hz)").c_str(),&s.editContext3D.frequency3D,1,10,"%.2f");
    if(s.ready3D) {
        AppUi::Widgets::Text("Active frequency: %.2f Hz | delay %.3f ms",s.context3D.frequency3D,1200/s.context3D.frequency3D);
        if(s.editContext3D.frequency3D!=s.context3D.frequency3D)
            AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Warning),"Frequency edit pending; current wavefield uses the active value.");
        const double frequency=s.editContext3D.frequency3D;
        if(std::isfinite(frequency)&&frequency>0) {
            const double interval=s.sampleIntervalInMs3D?s.requestedSampleIntervalMs3D*.001:double(s.context3D.dt3D)*s.samplingStride3D;
            const double duration=interval*s.targetSamples3D;
            AppUi::Widgets::Text("At edited f0: %.1f solver steps / period",1/(frequency*s.context3D.dt3D));
            if(interval>0&&s.targetSamples3D>0) {
                AppUi::Widgets::Text("Capture: %.3f ms interval | %.3f s duration",interval*1000,duration);
                if(frequency*interval>=.5) AppUi::Widgets::TextWrapped("Edited f0 reaches the recording Nyquist limit; reduce sample stride or frequency.");
                if(1.2/frequency>=duration) AppUi::Widgets::TextWrapped("Wavelet peak falls outside the requested capture window; increase samples or frequency.");
            }
            if(frequency*s.context3D.dt3D>.1) AppUi::Widgets::TextWrapped("Fewer than 10 solver steps per dominant period; time resolution may be insufficient.");
        }
    }
    AppUi::Widgets::TextWrapped("Apply frequency, Reset wavefield + fire, or Capture applies the wavelet and CPML together. Run resumes the active wavefield.");
    ImGui::BeginDisabled(!s.ready3D||s.recording3D||s.sequence3D);
    if(functionButton3D("Apply frequency + reset fields",AppUi::FunctionRole::Source,ImVec2(-1,0))) performAction3D(s,[&]{
        restartSimulation3D(s,false);s.message3D="Source frequency and CPML applied; fields reset. Model, receivers and display settings retained.";
    });
    ImGui::EndDisabled();
}

// Edit solver timing as a rebuild-only parameter and preview the same CFL validation used at allocation.
void drawTimeStepControls3D(Seis3D_State& s) {
    auto& c=s.editContext3D;
    int mode=c.manualTimeStep3D?1:0;
    if(AppUi::Widgets::Combo(fieldLabel3D("Solver time step").c_str(),&mode,"Automatic CFL\0Manual (ms)\0"))
        c.manualTimeStep3D=mode==1;
    if(c.manualTimeStep3D) {
        AppUi::Widgets::InputFloat(fieldLabel3D("Requested dt (ms)").c_str(),&c.requestedDtMs3D,.001f,.1f,"%.3f");
        if(AppUi::Widgets::SmallButton("0.25 ms")) c.requestedDtMs3D=.25f;
        ImGui::SameLine();if(AppUi::Widgets::SmallButton("0.5 ms")) c.requestedDtMs3D=.5f;
        ImGui::SameLine();if(AppUi::Widgets::SmallButton("1 ms")) c.requestedDtMs3D=1;
    }
    try {
        auto ceiling=c;ceiling.manualTimeStep3D=false;validateContext3D(ceiling);
        AppUi::Widgets::Text("CFL ceiling: %.3f ms",ceiling.safeDt3D*1000);
        auto candidate=c;validateContext3D(candidate);
        const auto dtUs=std::llround(double(candidate.dt3D)*1e6);
        const auto intervalUs=receiverSamplingIntervalUs3D(s,candidate.dt3D);
        AppUi::Widgets::Text("After rebuild: %.3f ms solver dt",dtUs/1000.0);
        AppUi::Widgets::Text("Sampling: %.3f ms | nominal window %.3f s",intervalUs/1000.0,
            double(intervalUs)*s.targetSamples3D/1e6);
        if(intervalUs>maxSegyIntervalUs3D)
            AppUi::Widgets::TextWrapped("Recording interval exceeds 32.767 ms; reduce sample stride or solver dt before capture.");
    } catch(const std::exception& e) {
        AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Danger),"%s",e.what());
    }
    if(s.ready3D) {
        AppUi::Widgets::Text("Active solver dt: %.3f ms",s.context3D.dt3D*1000);
        if(c.manualTimeStep3D!=s.context3D.manualTimeStep3D
            || (c.manualTimeStep3D&&c.requestedDtMs3D!=s.context3D.requestedDtMs3D))
            AppUi::Widgets::TextWrapped("Time-step edit pending. Rebuild the session to apply it; Run and Capture use the active dt.");
    }
    AppUi::Widgets::TextWrapped("Manual dt uses 0.001 ms increments and must not exceed the CFL ceiling. Time-step edits require Rebuild 3D session.");
}

// Show bounded grid presets and explicit material/CFL controls without mutating a live model.
void drawSimulationControls3D(Seis3D_State& s) {
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Simulation);
    auto& c=s.editContext3D;
    ImGui::BeginDisabled(s.recording3D);
    if(functionButton3D(s.ready3D?"Rebuild 3D session":"Initialize 3D",AppUi::FunctionRole::Model,ImVec2(-1,0))) performAction3D(s,[&]{initializeSession3D(s,false);});
    AppUi::Widgets::Text("Grid %d x %d x %d | %.1f m",c.nx3D,c.ny3D,c.nz3D,c.spacing3D);
    if(s.ready3D) AppUi::Widgets::TextWrapped("Live boundary: %s",boundaryName3D(s.context3D));
    if(sectionHeader3D("GRID / RESOLUTION")) {
    AppUi::Widgets::TextUnformatted("Grid presets");
    if(ImGui::BeginTable("GridPresets3D",2,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();if(AppUi::Widgets::Button("500/20/500",ImVec2(-1,0))) {c.nx3D=c.nz3D=500;c.ny3D=20;}
        ImGui::TableNextColumn();if(AppUi::Widgets::Button("200 cubed",ImVec2(-1,0))) c.nx3D=c.ny3D=c.nz3D=200;
        ImGui::TableNextColumn();if(AppUi::Widgets::Button("300 cubed",ImVec2(-1,0))) c.nx3D=c.ny3D=c.nz3D=300;
        ImGui::TableNextColumn();if(AppUi::Widgets::Button("300/300/400",ImVec2(-1,0))) {c.nx3D=c.ny3D=300;c.nz3D=400;}
        ImGui::EndTable();
    }
    int sizes[3]={c.nx3D,c.ny3D,c.nz3D};
    if(AppUi::Widgets::InputInt3(fieldLabel3D("Nodes X/Y/Z").c_str(),sizes)) { c.nx3D=sizes[0];c.ny3D=sizes[1];c.nz3D=sizes[2]; }
    AppUi::Widgets::TextWrapped("Maximum physical grid: X x Y x Z <= 36,000,000; each axis >= 16. VRAM and texture limits also apply.");
    if(c.nx3D>=16&&c.ny3D>=16&&c.nz3D>=16) {
        const double nodes=double(c.nx3D)*c.ny3D*c.nz3D;
        AppUi::Widgets::TextWrapped("%.3f / 36.000 M physical nodes",nodes/1e6);
        ImGui::ProgressBar(float((std::min)(nodes/double(maxPhysicalNodes3D),1.0)),ImVec2(-1,0));
        AppUi::Widgets::TextWrapped("Current axis limits X/Y/Z: %llu / %llu / %llu",
            static_cast<unsigned long long>(maxPhysicalNodes3D/(std::size_t(c.ny3D)*c.nz3D)),
            static_cast<unsigned long long>(maxPhysicalNodes3D/(std::size_t(c.nx3D)*c.nz3D)),
            static_cast<unsigned long long>(maxPhysicalNodes3D/(std::size_t(c.nx3D)*c.ny3D)));
        if(nodes>double(maxPhysicalNodes3D)) AppUi::Widgets::TextWrapped("Reduce a dimension before rebuilding. The total node budget is exceeded.");
    }
    AppUi::Widgets::InputFloat(fieldLabel3D("Grid spacing (m)").c_str(),&c.spacing3D,1,10,"%.3f");
    }
    if(functionHeader3D("BOUNDARY CONDITIONS",AppUi::FunctionRole::Warning)) {
        AppUi::FunctionStyleScope boundaryColors(AppUi::FunctionRole::Warning);
        int boundary=c.reflecting3D?2:c.freeSurface3D?0:1;
        if(AppUi::Widgets::Combo(fieldLabel3D("Boundary mode").c_str(),&boundary,"Free top + CPML\0All absorbing (6-face CPML)\0All reflecting (slip walls)\0")) {
            c.reflecting3D=boundary==2;c.freeSurface3D=boundary==0;
        }
        ImGui::BeginDisabled(c.reflecting3D);
        AppUi::Widgets::SliderInt(fieldLabel3D("CPML thickness (cells)").c_str(),&c.pml3D,8,40);
        ImGui::EndDisabled();
        AppUi::Widgets::TextWrapped(c.reflecting3D?"Six reflecting slip walls: normal velocity = 0, shear traction = 0. CPML is disabled.":
            c.freeSurface3D?"Traction-free top z=0; CPML on the other five faces.":"CPML on all six faces, including the top. Finite absorbing layers have residual reflections.");
    }
    if(functionHeader3D("MODEL / MATERIAL",AppUi::FunctionRole::Model)) {
    AppUi::FunctionStyleScope modelColors(AppUi::FunctionRole::Model);
    const ModelKind3D models[]={ModelKind3D::Uniform,ModelKind3D::TwoLayer,ModelKind3D::Sphere,ModelKind3D::ThreeLayer,
        ModelKind3D::DippingLayer,ModelKind3D::LowVelocityLayer,ModelKind3D::Fault,ModelKind3D::Gradient,ModelKind3D::SaltDome};
    int kind=-1;for(int i=0;i<9;++i) if(c.modelKind3D==models[i]) kind=i;
    if(AppUi::Widgets::Combo(fieldLabel3D("Model preset").c_str(),&kind,"Uniform\0Two layers\0Sphere anomaly\0Three layers\0Dipping interface\0Low-velocity interbed\0Faulted interface\0Depth velocity gradient\0Salt dome\0"))
        applyModelPreset3D(c,models[kind]);
    AppUi::Widgets::TextWrapped("%s",modelDescription3D(c.modelKind3D));
    AppUi::Widgets::InputFloat(fieldLabel3D("Vp (m/s)").c_str(),&c.vp3D,100,1000,"%.1f");
    AppUi::Widgets::InputFloat(fieldLabel3D("Vs (m/s)").c_str(),&c.vs3D,100,1000,"%.1f");
    AppUi::Widgets::InputFloat(fieldLabel3D("Density (kg/m3)").c_str(),&c.density3D,100,1000,"%.1f");
    if(c.modelKind3D!=ModelKind3D::Uniform && sectionHeader3D("Secondary / bottom material")) {
        AppUi::Widgets::InputFloat(fieldLabel3D("Secondary Vp").c_str(),&c.layerVp3D,100,1000,"%.1f");
        AppUi::Widgets::InputFloat(fieldLabel3D("Secondary Vs").c_str(),&c.layerVs3D,100,1000,"%.1f");
        AppUi::Widgets::InputFloat(fieldLabel3D("Secondary density").c_str(),&c.layerDensity3D,100,1000,"%.1f");
    }
    if(c.modelKind3D!=ModelKind3D::Uniform&&c.modelKind3D!=ModelKind3D::Imported
        &&c.modelKind3D!=ModelKind3D::ThreeLayer&&c.modelKind3D!=ModelKind3D::Gradient
        &&sectionHeader3D("Model geometry (domain fractions)")) {
        if(c.modelKind3D!=ModelKind3D::Sphere&&c.modelKind3D!=ModelKind3D::SaltDome)
            AppUi::Widgets::SliderFloat(fieldLabel3D("Interface depth").c_str(),&c.interfaceDepth3D,.1f,.9f,"%.2f");
        if(c.modelKind3D==ModelKind3D::DippingLayer||c.modelKind3D==ModelKind3D::Fault)
            AppUi::Widgets::SliderFloat(fieldLabel3D("Dip: depth change across X").c_str(),&c.layerDip3D,-.6f,.6f,"%.2f");
        if(c.modelKind3D==ModelKind3D::LowVelocityLayer)
            AppUi::Widgets::SliderFloat(fieldLabel3D("Interbed thickness").c_str(),&c.layerThickness3D,.03f,.5f,"%.2f");
        if(c.modelKind3D==ModelKind3D::Fault)
            AppUi::Widgets::SliderFloat(fieldLabel3D("Fault vertical throw").c_str(),&c.faultThrow3D,-.4f,.4f,"%.2f");
        if(c.modelKind3D==ModelKind3D::Sphere||c.modelKind3D==ModelKind3D::SaltDome)
            AppUi::Widgets::SliderFloat(fieldLabel3D("Anomaly radius").c_str(),&c.anomalyRadius3D,.05f,.4f,"%.3f");
    }
    AppUi::Widgets::TextWrapped("Model / boundary edits take effect after Rebuild 3D session.");
    }
    if(functionHeader3D("TIME / STABILITY",AppUi::FunctionRole::Source,ImGuiTreeNodeFlags_None)) {
    AppUi::FunctionStyleScope timeColors(AppUi::FunctionRole::Source);
    drawSourceFrequencyControls3D(s);
    AppUi::Widgets::SliderFloat(fieldLabel3D("CFL safety factor").c_str(),&c.cfl3D,0.2f,0.9f,"%.2f");
    drawTimeStepControls3D(s);
    }
    try {
        auto preview=c; validateContext3D(preview); const auto budget=estimateMemory3D(preview);
        if(functionHeader3D("MEMORY / STABILITY",AppUi::FunctionRole::Warning)) {
        AppUi::Widgets::Text("Fields + materials: %.2f GiB",budget.fieldBytes3D/gibibyte3D);
        AppUi::Widgets::Text("Compact CPML: %.2f GiB",budget.cpmlBytes3D/gibibyte3D);
        AppUi::Widgets::TextColored(AppUi::functionColor(budget.totalBytes3D<=memoryLimit3D?AppUi::FunctionRole::Simulation:AppUi::FunctionRole::Danger),
            "Total estimate: %.2f / 4.00 GiB",budget.totalBytes3D/gibibyte3D);
        AppUi::Widgets::Text("Safe dt: %.3f ms / Vp max %.0f",preview.safeDt3D*1000,preview.maxVp3D);
        AppUi::Widgets::Text("After rebuild: %.3f ms solver dt",preview.dt3D*1000);
        AppUi::Widgets::TextWrapped("Includes padding, boundary display and staging.");
        }
        if(budget.totalBytes3D>memoryLimit3D) AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Danger),"Memory budget exceeded; reduce the grid before rebuilding.");
    } catch(const std::exception& e) { AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Danger),"%s",e.what()); }
    ImGui::EndDisabled();
    if(functionHeader3D("RUN / ITERATION",AppUi::FunctionRole::Simulation,ImGuiTreeNodeFlags_DefaultOpen)) {
    AppUi::Widgets::SliderInt(fieldLabel3D("Steps per frame").c_str(),&s.stepsPerFrame3D,1,8);
    AppUi::Widgets::SliderInt(fieldLabel3D("Stop after steps").c_str(),&s.maximumSteps3D,100,20000);
    AppUi::Widgets::TextWrapped("8th-order staggered interior / 2nd-order time. Planar free-surface closure: 2nd order.");
    }
}

// Configure a compact explosive or directed body-force source independently from the 2-D tools.
void drawSourceControls3D(Seis3D_State& s) {
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Source);
    ImGui::BeginDisabled(!s.ready3D||s.recording3D);
    const auto& c=s.context3D;
    AppUi::Widgets::Text("Source XYZ [%d,%d,%d]",s.source3D.x3D,s.source3D.y3D,s.source3D.z3D);
    if(functionHeader3D("SOURCE WAVELET",AppUi::FunctionRole::Source,ImGuiTreeNodeFlags_DefaultOpen)) drawSourceFrequencyControls3D(s);
    if(functionHeader3D("SOURCE POSITION",AppUi::FunctionRole::Source)) {
    AppUi::Widgets::SliderInt(fieldLabel3D("Source X").c_str(),&s.source3D.x3D,1,(std::max)(2,c.nx3D-2));
    AppUi::Widgets::SliderInt(fieldLabel3D("Source Y").c_str(),&s.source3D.y3D,1,(std::max)(2,c.ny3D-2));
    AppUi::Widgets::SliderInt(fieldLabel3D("Source depth Z").c_str(),&s.source3D.z3D,2,(std::max)(3,c.nz3D-2));
    }
    if(functionHeader3D("SOURCE MECHANISM",AppUi::FunctionRole::Source)) {
    int kind=int(s.source3D.kind3D);
    if(AppUi::Widgets::Combo(fieldLabel3D("Source mechanism").c_str(),&kind,"Isotropic explosion\0Directed body force\0")) s.source3D.kind3D=SourceKind3D(kind);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Amplitude").c_str(),&s.source3D.amplitude3D,1e3f,1e12f,"%.3e",ImGuiSliderFlags_Logarithmic);
    AppUi::Widgets::TextDisabled(s.source3D.kind3D==SourceKind3D::Explosion?"Amplitude unit: Pa/s":"Amplitude unit: N/m3");
    if(s.source3D.kind3D==SourceKind3D::Force) {
        AppUi::Widgets::InputFloat3(fieldLabel3D("Direction XYZ").c_str(),s.source3D.direction3D);
        if(AppUi::Widgets::Button("Normalize direction")) performAction3D(s,[&]{
            const auto d=s.source3D.direction3D; const float norm=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            if(!std::isfinite(norm)||norm<1e-10f) throw std::runtime_error("Force direction must be finite and non-zero.");
            for(auto& value:s.source3D.direction3D) value/=norm;
        });
    }
    }
    if(AppUi::Widgets::Button("Center slices on source",ImVec2(-1,0))) {
        s.slice3D[0]=s.source3D.x3D;s.slice3D[1]=s.source3D.y3D;s.slice3D[2]=s.source3D.z3D;s.dirty3D=true;
    }
    if(AppUi::Widgets::Button("Reset wavefield + fire",ImVec2(-1,0))) performAction3D(s,[&]{
        restartSimulation3D(s,true);
    });
    ImGui::EndDisabled();
    AppUi::Widgets::TextWrapped("A normalized 3 x 3 x 3 spatial source reduces grid-scale excitation. Wavelet delay is 1.2 / f0.");
}

// Import S3DM or export initialized SI materials through independent S3DM/VTI output paths.
void drawModelControls3D(Seis3D_State& s) {
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Model);
    AppUi::Widgets::TextWrapped(".s3dm: physical XYZ grid, SI Vp / Vs / rho.");
    ImGui::BeginDisabled(s.recording3D||s.sequence3D);
    AppUi::Widgets::InputText(fieldLabel3D("Model path").c_str(),s.modelPath3D,sizeof(s.modelPath3D));
    if(AppUi::Widgets::Button("Browse model file...",ImVec2(-1,0))) performAction3D(s,[&]{browseModelPath3D(s,false);});
    if(AppUi::Widgets::Button("Validate + import 3D model",ImVec2(-1,0))) performAction3D(s,[&]{initializeSession3D(s,true);});
    ImGui::Separator();
    AppUi::Widgets::Combo(fieldLabel3D("Model export format").c_str(),&s.modelExportFormat3D,
        "Seis3D model (.s3dm)\0VTK ImageData (.vti)\0");
    const bool vti3D=s.modelExportFormat3D==1;
    auto& outputPath3D=vti3D?s.vtiModelExportPath3D:s.modelExportPath3D;
    AppUi::Widgets::InputText(fieldLabel3D("Model export path").c_str(),outputPath3D,sizeof(outputPath3D));
    if(AppUi::Widgets::Button("Choose model output...",ImVec2(-1,0))) performAction3D(s,[&]{
        if(vti3D) browseVtiModelPath3D(s);else browseModelPath3D(s,true);
    });
    ImGui::BeginDisabled(!s.ready3D);
    if(AppUi::Widgets::Button(vti3D?"Export VTI model to a new file":"Export model to a new file",ImVec2(-1,0)))
        performAction3D(s,[&]{if(vti3D) exportVtiModel3D(s);else exportModel3D(s);});
    ImGui::EndDisabled();
    if(vti3D) AppUi::Widgets::TextWrapped("VTI: Vp / Vs (m/s), Density (kg/m3); Z increases with depth.");
    ImGui::Separator();
    AppUi::Widgets::Text("Current wavefield snapshot: %s",s.component3D==0?"Vz":s.component3D==1?"Vx":s.component3D==2?"Vy":s.component3D==3?"Velocity magnitude":"Pressure");
    AppUi::Widgets::InputText(fieldLabel3D("Wavefield snapshot path").c_str(),s.vtiWavefieldExportPath3D,sizeof(s.vtiWavefieldExportPath3D));
    if(AppUi::Widgets::Button("Choose snapshot output...",ImVec2(-1,0))) performAction3D(s,[&]{browseVtiWavefieldPath3D(s);});
    ImGui::BeginDisabled(!s.ready3D);
    if(AppUi::Widgets::Button("Export current wavefield snapshot (.vti)",ImVec2(-1,0)))
        performAction3D(s,[&]{exportVtiWavefield3D(s);});
    ImGui::EndDisabled();
    AppUi::Widgets::TextWrapped("Exports the selected physical component at native grid resolution and current simulation time. Display gain and palette do not change saved values.");
    ImGui::EndDisabled();
    if(functionHeader3D("MODEL FILE DETAILS",AppUi::FunctionRole::Model)) {
    AppUi::Widgets::TextWrapped("The importer scans every material cell and computes maximum Vp before allocating GPU memory. Large models are transferred one depth plane at a time.");
    AppUi::Widgets::TextDisabled("Exports preserve existing files; choose a new name.");
    }
}

// Restore bounded volume recipes; independent gain is explicitly shown with its own amplitude range.
void applyVolumeLook3D(Seis3D_State& s,int look) {
    s.volumeLook3D=(std::clamp)(look,0,2);s.volumeSizeCompensation3D=true;
    s.volumeSampling3D=1.25f;
    s.volumeGain3D=look==1?3.0f:look==2?1.5f:1.8f;
    s.volumeOpacity3D=look==1?2.6f:look==2?1.2f:2.0f;
    s.volumeThreshold3D=look==1?.012f:look==2?.02f:.018f;
    s.volumeSharpness3D=look==1?1.1f:look==2?.85f:1.15f;
}

// Switch colors independently from amplitude, composition, component, ranges and volume settings.
void applyVisualPreset3D(Seis3D_State& s,int preset) {
    s.visualPreset3D=(std::clamp)(preset,0,4);
    const int selected=s.visualPreset3D;
    s.waveStyle3D=selected==1?0:selected==2?1:selected==3?13:selected==4?1:7;
    s.modelStyle3D=selected==2?1:selected==4?4:0;
}

// Restore the selected preset only after an explicit reset, preserving simulation and recorded data.
void resetDisplayParameters3D(Seis3D_State& s) {
    const int preset=(std::clamp)(s.visualPreset3D,0,4);
    applyVisualPreset3D(s,preset);
    selectWaveComponent3D(s,preset==1||preset==2?0:3);
    s.displayMode3D=0;s.autoScale3D=true;s.holdPeakScale3D=false;s.manualScale3D=.01f;
    s.materialComponent3D=5;s.manualMaterialRange3D[0]=1500;s.manualMaterialRange3D[1]=5000;
    s.materialRangeAuto3D=true;s.waveOpacity3D=.95f;s.waveThreshold3D=.008f;s.waveGamma3D=.6f;
    s.materialBrightness3D=.65f;s.gain3D=1;s.modelOpacity3D=.12f;s.gridOpacity3D=.10f;
    s.showGrid3D=true;s.showCrosshair3D=true;s.boundaryOpacity3D=.12f;s.displayStride3D=1;
    if(s.showBoundary3D) {s.showBoundary3D=false;resetSliceViews3D(s);}
    applyVolumeLook3D(s,0);s.showVolume3D=true;
    if(preset==2) {s.materialBrightness3D=.9f;s.modelOpacity3D=.3f;s.waveGamma3D=.8f;}
    if(preset==4) {
        s.waveStyle3D=1;s.modelStyle3D=4;
        s.materialBrightness3D=1;s.modelOpacity3D=.16f;s.gridOpacity3D=.16f;
        s.gain3D=1.8f;s.waveOpacity3D=1;s.waveGamma3D=.5f;
        s.volumeOpacity3D=1.4f;s.volumeThreshold3D=.02f;
    }
    s.dirty3D=true;
}

// Copy the active amplitude reference before switching off automatic scaling, preserving brightness.
void setAutomaticScale3D(Seis3D_State& s,bool automatic) {
    const float measured=s.holdPeakScale3D?s.scalePeak3D:s.referenceMaximum3D;
    if(s.autoScale3D&&!automatic&&s.scaleComponent3D==s.component3D&&measured>0) s.manualScale3D=displayScale3D(s);
    s.autoScale3D=automatic;
}

// Switch physical quantities without clearing same-unit reference history or reusing Pa as m/s.
void selectWaveComponent3D(Seis3D_State& s,int component) {
    component=(std::clamp)(component,0,4);const int unit=component==4?1:0;
    if(unit!=s.scaleUnit3D) {
        s.unitScalePeaks3D[s.scaleUnit3D]=s.scalePeak3D;
        s.unitManualScales3D[s.scaleUnit3D]=s.manualScale3D;
        s.scalePeak3D=s.unitScalePeaks3D[unit];s.manualScale3D=s.unitManualScales3D[unit];
        s.referenceMaximum3D=0;s.scaleUnit3D=unit;
    }
    s.component3D=component;s.dirty3D=true;
}

// Configure independent geological material, signed wave transfer functions and rendering cadence.
void drawViewControls3D(Seis3D_State& s) {
    if(functionHeader3D("APPEARANCE / LANGUAGE",AppUi::FunctionRole::Display)) {
    AppUi::drawThemeSelector(fieldLabel3D("Interface theme").c_str(),-1);
    AppUi::Widgets::TextUnformatted("Language");AppUi::drawLanguageSelector(-1);
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Display);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Workspace text size").c_str(),&s.fontScale3D,.85f,1.25f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
    }
    int preset=s.visualPreset3D;
    if(AppUi::Widgets::Combo(fieldLabel3D("Visual preset").c_str(),&preset,"Wavefront contrast\0Signed seismic\0Geological layers\0CUDA glow\0Paper scientific\0")) applyVisualPreset3D(s,preset);
    AppUi::Widgets::TextWrapped("Preset changes colors only; your display parameters are retained.");
    if(functionButton3D("Reset display parameters",AppUi::FunctionRole::Danger,ImVec2(-1,0))) resetDisplayParameters3D(s);
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("Restore all display parameters to the selected preset defaults. Simulation and captured data are preserved.");
    if(functionHeader3D("WAVEFIELD / GAIN",AppUi::FunctionRole::Display,ImGuiTreeNodeFlags_DefaultOpen)) {
    AppUi::Widgets::Combo(fieldLabel3D("Display").c_str(),&s.displayMode3D,"Model + wavefield\0Wavefield only\0Material only\0");
    int component=s.component3D;
    if(AppUi::Widgets::Combo(fieldLabel3D("Wave component").c_str(),&component,"Vz\0Vx\0Vy\0Velocity magnitude\0Pressure\0")) selectWaveComponent3D(s,component);
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("W: next component | Shift+W: previous. Display gain is retained.");
    AppUi::Widgets::TextWrapped("Velocity components share one |V| reference. Pressure keeps its own Pa scale. Gains are retained.");
    if(s.component3D<3) AppUi::Widgets::TextWrapped("A signed component can be near zero on a symmetry plane. Use velocity magnitude to inspect all wavefronts.");
    AppUi::Widgets::Combo(fieldLabel3D("Wave palette").c_str(),&s.waveStyle3D,waveStyleNames3D);
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("Q: next wave palette | Shift+Q: previous.");
    AppUi::Widgets::SliderFloat(fieldLabel3D("Display gain").c_str(),&s.gain3D,0.1f,20.0f,"%.2f",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
    }
    if(functionHeader3D("AMPLITUDE REFERENCE",AppUi::FunctionRole::Display)) {
    bool automatic=s.autoScale3D;
    if(AppUi::Widgets::Checkbox("Automatic common scale",&automatic)) setAutomaticScale3D(s,automatic);
    if(s.autoScale3D) {
        AppUi::Widgets::Checkbox("Hold observed peak (stable colors)",&s.holdPeakScale3D);
        if(AppUi::Widgets::SmallButton("Reset observed peak")) {s.scalePeak3D=0;s.dirty3D=true;}
        AppUi::Widgets::Text("Component max: %.3e %s",s.viewMaximum3D,s.component3D==4?"Pa":"m/s");
        AppUi::Widgets::Text("Reference: %.3e | Held: %.3e",s.referenceMaximum3D,s.scalePeak3D);
        ImGui::BeginDisabled(s.referenceMaximum3D<=0);
        if(AppUi::Widgets::Button("Lock current amplitude scale",ImVec2(-1,0))) setAutomaticScale3D(s,false);
        ImGui::EndDisabled();
        AppUi::Widgets::TextWrapped("New model / source: statistics restart; gains are retained. Lock the scale to compare runs.");
    }
    if(!s.autoScale3D) {
        AppUi::Widgets::SliderFloat(fieldLabel3D("Amplitude scale").c_str(),&s.manualScale3D,1e-12f,1e10f,"%.3e",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
        AppUi::Widgets::TextWrapped("Fixed amplitude scale survives model rebuilds and source resets.");
    }
    }
    if(functionHeader3D("WAVEFIELD TRANSFER",AppUi::FunctionRole::Display)) {
    AppUi::Widgets::SliderFloat(fieldLabel3D("Wave opacity").c_str(),&s.waveOpacity3D,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Wave cutoff").c_str(),&s.waveThreshold3D,0,0.8f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Weak-wave visibility").c_str(),&s.waveGamma3D,.35f,1.5f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::PushTextWrapPos(0.0f);
    AppUi::Widgets::TextDisabled("Lower values reveal weak waves; samples retain SI amplitudes.");
    ImGui::PopTextWrapPos();
    }
    if(functionHeader3D("GEOLOGICAL MATERIAL",AppUi::FunctionRole::Model)) {
    int material=s.materialComponent3D-5;
    if(AppUi::Widgets::Combo(fieldLabel3D("Property").c_str(),&material,"Vp (m/s)\0Vs (m/s)\0Density (kg/m3)\0")) {s.materialComponent3D=material+5;s.dirty3D=true;}
    AppUi::Widgets::Combo(fieldLabel3D("Model palette").c_str(),&s.modelStyle3D,modelStyleNames3D);
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("E: next model palette | Shift+E: previous.");
    AppUi::Widgets::SliderFloat(fieldLabel3D("Model backdrop brightness").c_str(),&s.materialBrightness3D,.2f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::Checkbox("Auto material range (visible data)",&s.materialRangeAuto3D);
    if(!s.materialRangeAuto3D) {
        AppUi::Widgets::InputFloat2(fieldLabel3D("Range min / max").c_str(),s.manualMaterialRange3D,"%.1f");
        if(!std::isfinite(s.manualMaterialRange3D[0])||!std::isfinite(s.manualMaterialRange3D[1])) {s.manualMaterialRange3D[0]=0;s.manualMaterialRange3D[1]=1;}
        if(s.manualMaterialRange3D[1]<=s.manualMaterialRange3D[0]) AppUi::Widgets::TextWrapped("Set maximum above minimum for material contrast.");
    }
    if(AppUi::Widgets::Button("Freeze visible material range")) {
        std::copy_n(s.materialRange3D,2,s.manualMaterialRange3D);s.materialRangeAuto3D=false;
    }
    }
    if(functionHeader3D("GRID / BOUNDARY OVERLAY",AppUi::FunctionRole::Warning)) {
    AppUi::Widgets::Checkbox("Physical grid",&s.showGrid3D);ImGui::SameLine();AppUi::Widgets::Checkbox("Crosshairs",&s.showCrosshair3D);
    if(AppUi::Widgets::Checkbox("Show boundary layers / walls",&s.showBoundary3D)) {resetSliceViews3D(s);s.dirty3D=true;}
    if(s.showBoundary3D) {
        AppUi::Widgets::SliderFloat(fieldLabel3D("Boundary tint opacity").c_str(),&s.boundaryOpacity3D,0,.4f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        AppUi::Widgets::TextWrapped("Amber: actual external CPML cells. Outline: physical model. Reflecting mode has walls only. Visibility does not change the solver.");
    }
    AppUi::Widgets::SliderFloat(fieldLabel3D("Grid opacity").c_str(),&s.gridOpacity3D,0,0.6f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    }
    if(functionHeader3D("VOLUME / PERFORMANCE",AppUi::FunctionRole::Display)) {
    if(AppUi::Widgets::Checkbox("Auxiliary volume raymarch",&s.showVolume3D)) s.dirty3D=true;
    int volumeLook=s.volumeLook3D;
    if(AppUi::Widgets::Combo(fieldLabel3D("Volume recipe").c_str(),&volumeLook,"Balanced\0Vivid wavefronts\0Soft cloud\0")) applyVolumeLook3D(s,volumeLook);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Volume gain (display only)").c_str(),&s.volumeGain3D,.25f,32.0f,"%.2fx",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::Checkbox("Size-consistent wave opacity",&s.volumeSizeCompensation3D);
    AppUi::Widgets::TextWrapped("Compensates thin wavefronts in larger boxes. Volume gain has its own color range; slices and recorded amplitudes keep their settings.");
    AppUi::Widgets::SliderFloat(fieldLabel3D("Cloud opacity").c_str(),&s.volumeOpacity3D,0.1f,8.0f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Cloud cutoff").c_str(),&s.volumeThreshold3D,0.0f,0.9f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Ray samples / voxel").c_str(),&s.volumeSampling3D,0.5f,2.0f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Wavefront sharpness").c_str(),&s.volumeSharpness3D,0.5f,3.0f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    if(s.ready3D) AppUi::Widgets::TextWrapped("Volume %d x %d x %d | Output %d x %d pixels",
        s.gl3D.layout3D.volumeX3D,s.gl3D.layout3D.volumeY3D,s.gl3D.layout3D.volumeZ3D,s.gl3D.volumeWidth3D,s.gl3D.volumeHeight3D);
    AppUi::Widgets::SliderFloat(fieldLabel3D("Volume model opacity").c_str(),&s.modelOpacity3D,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::SliderInt(fieldLabel3D("Display every N steps").c_str(),&s.displayStride3D,1,16,"%d",ImGuiSliderFlags_AlwaysClamp);
    AppUi::Widgets::TextDisabled("Display cadence preserves dt and receiver sampling.");
    }
    if(functionHeader3D("VIEW NAVIGATION",AppUi::FunctionRole::Navigation)) {
    if(AppUi::Widgets::Button("Reset all slice views",ImVec2(-1,0))) resetSliceViews3D(s);
    if(AppUi::Widgets::Button("Reset orbit camera",ImVec2(-1,0))) s.camera3D=VolumeCamera3D{};
    AppUi::Widgets::TextWrapped("Slices: wheel zooms at cursor; middle drag pans; left click links positions. Each Reset view fits its slice. Volume: left drag orbits, right drag / wheel zooms, middle drag pans. R resets all views.");
    }
}

// Show the exact transfer palette and physical range separately from display opacity.
void drawLegend3D(Seis3D_State& s) {
    if(!functionHeader3D("COLOR / PHYSICAL SCALE",AppUi::FunctionRole::Display)) return;
    const char* components[]={"Vz","Vx","Vy","|V|","Pressure"};
    const float scale=displayScale3D(s)/(std::max)(s.gain3D,0.001f);
    if(s.displayMode3D!=2) {
        AppUi::Widgets::Text("%s  [%s]",components[s.component3D],s.component3D==4?"Pa":"m/s");
        const auto start=ImGui::GetCursorScreenPos();
        const float width=(std::max)(1.0f,ImGui::GetContentRegionAvail().x);
        for(int i=0;i<64;++i) ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            ImVec2(start.x+width*i/64,start.y),ImVec2(start.x+width*(i+1)/64,start.y+12),
            paletteColor3D(s.waveStyle3D,float(i)/64),paletteColor3D(s.waveStyle3D,float(i+1)/64),
            paletteColor3D(s.waveStyle3D,float(i+1)/64),paletteColor3D(s.waveStyle3D,float(i)/64));
        ImGui::Dummy(ImVec2(width,14));
        AppUi::Widgets::Text("%.2e  ...  %.2e",s.component3D==3?0.0f:-scale,scale);
        AppUi::Widgets::TextDisabled("Slice range before transparency.");
        if(s.showVolume3D) {
            const float volumeScale=scale/(std::max)(s.volumeGain3D,.001f);
            AppUi::Widgets::TextWrapped("Volume x%.2f: %.2e .. %.2e %s",s.volumeGain3D,s.component3D==3?0.0f:-volumeScale,
                volumeScale,s.component3D==4?"Pa":"m/s");
        }
    }
    if(s.displayMode3D!=1) {
        const auto* range=s.materialRangeAuto3D?s.materialRange3D:s.manualMaterialRange3D;
        AppUi::Widgets::Text("%s: %.1f .. %.1f %s",s.materialComponent3D==5?"Vp":s.materialComponent3D==6?"Vs":"Density",
            range[0],range[1],s.materialComponent3D==7?"kg/m3":"m/s");
    }
}

// Configure surface receiver bins and multi-line shots, locking geometry for each active capture.
void drawSamplingControls3D(Seis3D_State& s) {
    AppUi::Widgets::InputInt(fieldLabel3D("Samples per shot").c_str(),&s.targetSamples3D);
    int samplingMode=s.sampleIntervalInMs3D?0:1;
    if(AppUi::Widgets::Combo(fieldLabel3D("Sampling clock").c_str(),&samplingMode,"Uniform interval (ms)\0Solver stride (steps)\0"))
        s.sampleIntervalInMs3D=samplingMode==0;
    if(AppUi::Widgets::InputDouble(fieldLabel3D("Sampling interval (ms)").c_str(),&s.requestedSampleIntervalMs3D,0.1,1.0,"%.3f"))
        s.sampleIntervalInMs3D=true;
    if(AppUi::Widgets::Button("0.5 ms##sample3D")) {s.requestedSampleIntervalMs3D=0.5;s.sampleIntervalInMs3D=true;}
    ImGui::SameLine();
    if(AppUi::Widgets::Button("1 ms##sample3D")) {s.requestedSampleIntervalMs3D=1.0;s.sampleIntervalInMs3D=true;}
    try {
        s.samplingStride3D=receiverSamplingStride3D(s,s.context3D.dt3D);
        AppUi::Widgets::Text("Actual sample interval: %.3f ms",receiverSamplingIntervalUs3D(s,s.context3D.dt3D)/1000.0);
    } catch(const std::exception& e) {
        AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Danger),"%s",e.what());
    }
    ImGui::BeginDisabled(s.sampleIntervalInMs3D);
    if(AppUi::Widgets::InputInt(fieldLabel3D("Sample stride (steps)").c_str(),&s.samplingStride3D)) {
        s.samplingStride3D=(std::clamp)(s.samplingStride3D,1,10000);
        s.sampleIntervalInMs3D=false;
    }
    ImGui::EndDisabled();
    AppUi::Widgets::TextWrapped("Uniform output times use linear interpolation between solver steps; solver dt is unchanged.");
}

void drawAcquisitionControls3D(Seis3D_State& s) {
    drawShotImportControls3D(s);
    auto& q=s.survey3D;
    ImGui::BeginDisabled(!s.ready3D||s.recording3D);
    if(functionHeader3D("SURFACE RECEIVER MESH",AppUi::FunctionRole::Acquisition)) {
    AppUi::Widgets::Text("Receiver channels: %d / %d",q.receiverCountX3D*q.receiverCountY3D,maxReceivers3D);
    AppUi::Widgets::TextDisabled("Total channels = X x Y; each stores Vx / Vy / Vz.");
    if(AppUi::Widgets::Button("100 x 5 / 500",ImVec2(-1,0))) {q.receiverCountX3D=100;q.receiverCountY3D=5;fitSurvey3D(s);}
    if(AppUi::Widgets::Button("50 x 3 / 150",ImVec2(-1,0))) {q.receiverCountX3D=50;q.receiverCountY3D=3;fitSurvey3D(s);}
    if(AppUi::Widgets::Button("50 x 6 / 300",ImVec2(-1,0))) {q.receiverCountX3D=50;q.receiverCountY3D=6;fitSurvey3D(s);}
    if(AppUi::Widgets::Button("8 x 8 / 64")) {q.receiverCountX3D=q.receiverCountY3D=8;fitSurvey3D(s);}
    ImGui::SameLine();if(AppUi::Widgets::Button("10 x 10 / 100")) {q.receiverCountX3D=q.receiverCountY3D=10;fitSurvey3D(s);}
    if(AppUi::Widgets::Button("20 x 5")) {q.receiverCountX3D=20;q.receiverCountY3D=5;fitSurvey3D(s);}
    ImGui::SameLine();if(AppUi::Widgets::Button("5 x 20")) {q.receiverCountX3D=5;q.receiverCountY3D=20;fitSurvey3D(s);}
    if(AppUi::Widgets::Button("X line (fit)")) {q.receiverCountX3D=maxReceivers3D;q.receiverCountY3D=1;fitSurvey3D(s);}
    ImGui::SameLine();if(AppUi::Widgets::Button("Y line (fit)")) {q.receiverCountX3D=1;q.receiverCountY3D=maxReceivers3D;fitSurvey3D(s);}
    AppUi::Widgets::TextWrapped("Presets fit the grid; Deploy mesh applies edits. Receiver traces can show all channels or one X / Y row.");
    AppUi::Widgets::TextWrapped("Default: 100 X receivers in 5 Y rows (500 total). Fit spread centers the mesh; smaller grids reduce the fitted count.");
    if(AppUi::Widgets::InputInt(fieldLabel3D("Receiver count X").c_str(),&q.receiverCountX3D)) {
        q.receiverCountX3D=(std::clamp)(q.receiverCountX3D,1,maxReceivers3D);
        q.receiverCountY3D=(std::clamp)(q.receiverCountY3D,1,maxReceivers3D/q.receiverCountX3D);
    }
    if(AppUi::Widgets::InputInt(fieldLabel3D("Receiver count Y").c_str(),&q.receiverCountY3D))
        q.receiverCountY3D=(std::clamp)(q.receiverCountY3D,1,maxReceivers3D/(std::max)(1,q.receiverCountX3D));
    AppUi::Widgets::InputInt3(fieldLabel3D("Receiver origin XYZ").c_str(),q.receiverOrigin3D); AppUi::Widgets::InputInt2(fieldLabel3D("Receiver spacing XY").c_str(),q.receiverSpacing3D);
    if(AppUi::Widgets::Button("Fit spread to model")) { fitSurvey3D(s); }
    ImGui::SameLine(); if(AppUi::Widgets::Button("Deploy mesh")) performAction3D(s,[&]{configureReceivers3D(s);});
    }
    if(functionHeader3D("WORLD COORDINATES / CMP",AppUi::FunctionRole::Acquisition)) {
    AppUi::Widgets::InputDouble(fieldLabel3D("World origin X (m)").c_str(),&q.worldOrigin3D[0],0,0,"%.3f");
    AppUi::Widgets::InputDouble(fieldLabel3D("World origin Y (m)").c_str(),&q.worldOrigin3D[1],0,0,"%.3f");
    AppUi::Widgets::InputFloat2(fieldLabel3D("CMP bin size XY (m)").c_str(),q.cmpBinSize3D,"%.2f");
    }
    if(functionHeader3D("RECORDING / EXPORT",AppUi::FunctionRole::Acquisition)) {
    drawSamplingControls3D(s);
    AppUi::Widgets::InputText(fieldLabel3D("Export folder").c_str(),s.exportDirectory3D,sizeof(s.exportDirectory3D));
    if(AppUi::Widgets::Button("Browse export folder...",ImVec2(-1,0))) performAction3D(s,[&]{browseExportFolder3D(s);});
    AppUi::Widgets::InputText(fieldLabel3D("Shot file prefix").c_str(),s.exportPrefix3D,sizeof(s.exportPrefix3D));
    if(AppUi::Widgets::Button("Choose export name / location...",ImVec2(-1,0))) performAction3D(s,[&]{browseGatherDestination3D(s);});
    AppUi::Widgets::Checkbox("Create session subfolder",&s.exportSessionFolder3D);
    AppUi::Widgets::TextWrapped("Output: prefix_0001_vx.sgy / vy.sgy / vz.sgy and prefix_0001_geometry.csv. Existing files are preserved.");
    AppUi::Widgets::TextDisabled("Current prefix: %s",s.exportPrefix3D);
    AppUi::Widgets::TextWrapped("With session subfolders off, files are written directly into the selected export folder.");
    }
    if(AppUi::Widgets::Button("Capture current source",ImVec2(-1,0))) performAction3D(s,[&]{s.sequence3D=false;startGather3D(s,1);});
    if(functionHeader3D("MULTI-LINE SOURCE SPREAD",AppUi::FunctionRole::Acquisition)) {
    AppUi::Widgets::InputInt(fieldLabel3D("Shots per line").c_str(),&q.shotCountX3D); AppUi::Widgets::InputInt(fieldLabel3D("Source line count").c_str(),&q.shotCountY3D);
    AppUi::Widgets::InputInt3(fieldLabel3D("First shot XYZ").c_str(),q.shotOrigin3D); AppUi::Widgets::InputInt2(fieldLabel3D("Shot spacing XY").c_str(),q.shotSpacing3D);
    if(AppUi::Widgets::Button("Start spread + auto export",ImVec2(-1,0))) performAction3D(s,[&]{
        if(!s.exportDirectory3D[0]) throw std::runtime_error("Choose an export folder.");
        validatedExportPrefix3D(s.exportPrefix3D);
        s.sequenceIndex3D=0;positionSequenceShot3D(s);
        s.sequenceDirectory3D=uiPath3D(s.exportDirectory3D);
        if(s.exportSessionFolder3D) s.sequenceDirectory3D/="survey_"+sessionSuffix3D();
        std::filesystem::create_directories(s.sequenceDirectory3D);
        startGather3D(s,1);s.sequence3D=true;
    });
    }
    ImGui::EndDisabled();
    if(s.recording3D) {
        ImGui::ProgressBar(float(s.gather3D.sampleCount3D)/s.gather3D.targetSamples3D,ImVec2(-1,0));
        AppUi::Widgets::Text("Shot %d / samples %d of %d",s.gather3D.shotId3D,s.gather3D.sampleCount3D,s.gather3D.targetSamples3D);
        if(functionButton3D("Stop capture / spread",AppUi::FunctionRole::Danger)) { s.recording3D=false;s.sequence3D=false;s.running3D=false;s.message3D="Capture stopped; partial gather retained."; }
    }
    ImGui::BeginDisabled(s.recording3D||s.gather3D.sampleCount3D==0);
    if(AppUi::Widgets::Button("Export captured 3-C SEG-Y",ImVec2(-1,0))) performAction3D(s,[&]{
        if(!s.exportDirectory3D[0]) throw std::runtime_error("Choose an export folder.");
        auto directory=uiPath3D(s.exportDirectory3D);
        if(s.exportSessionFolder3D) directory/="capture_"+sessionSuffix3D();
        exportGather3D(s,directory);
    });
    ImGui::EndDisabled();
}

// Fit a physical slice into a panel without stretching its spatial aspect ratio.
ImVec2 fittedImageSize3D(float ratio,ImVec2 available) {
    const float width=(std::max)(1.0f,available.x),height=(std::max)(1.0f,available.y);
    return width/height>ratio ? ImVec2(height*ratio,height) : ImVec2(width,width/ratio);
}

// Draw the bounded raymarching exploration view and own all orbit-camera gestures locally.
void drawVolumePanel3D(Seis3D_State& s) {
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Display);
    ImGui::BeginChild("volumePanel3D",ImVec2(0,0),ImGuiChildFlags_None);
    if(!s.ready3D||!s.showVolume3D) {
        AppUi::Widgets::TextWrapped("Enable the auxiliary volume in Slice View. The three slices always use full-resolution solver data.");
    } else {
        if(AppUi::Widgets::SmallButton("Reset camera")) s.camera3D=VolumeCamera3D{};
        ImGui::SameLine();AppUi::Widgets::TextDisabled("LMB orbit / RMB zoom / MMB pan");
        const auto available=ImGui::GetContentRegionAvail();
        const ImVec2 size((std::max)(available.x,1.0f),(std::max)(available.y,1.0f));
        const auto framebufferScale=ImGui::GetIO().DisplayFramebufferScale;
        // Keep the target's aspect ratio while bounding the pixel budget on high-DPI displays.
        const float width=size.x*framebufferScale.x,height=size.y*framebufferScale.y;
        const float factor=(std::min)(1.0f,float(maxVolumeOutput3D)/(std::max)(width,height));
        s.volumeRequestedWidth3D=(std::clamp)(int(std::ceil(width*factor)),1,maxVolumeOutput3D);
        s.volumeRequestedHeight3D=(std::clamp)(int(std::ceil(height*factor)),1,maxVolumeOutput3D);
        ImGui::Image(ImTextureID(s.gl3D.volumeDisplayTexture3D),size,ImVec2(0,1),ImVec2(1,0));
        if(ImGui::IsItemHovered()) {
            const auto& io=ImGui::GetIO();
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {s.camera3D.yaw3D-=io.MouseDelta.x*0.008f;s.camera3D.pitch3D+=io.MouseDelta.y*0.008f;}
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)) s.camera3D.distance3D*=std::exp(io.MouseDelta.y*0.01f);
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {s.camera3D.pan3D[0]-=io.MouseDelta.x*0.002f;s.camera3D.pan3D[1]+=io.MouseDelta.y*0.002f;}
            s.camera3D.distance3D*=std::exp(-io.MouseWheel*0.15f);
            s.camera3D.distance3D=(std::clamp)(s.camera3D.distance3D,1.0f,8.0f);
            s.camera3D.pitch3D=(std::clamp)(s.camera3D.pitch3D,-1.45f,1.45f);
        }
    }
    ImGui::EndChild();
}

// Continue a toolbar row only when the next control fits; narrow windows wrap at control boundaries.
void continueToolbar3D(const char* nextLabel) {
    const float needed=AppUi::Widgets::CalcTextSize(nextLabel).x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetStyle().ItemSpacing.x;
    const float right=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;
    if(ImGui::GetItemRectMax().x+needed<right) ImGui::SameLine();
}

} // namespace

using namespace Detail3D;

void drawBackendControls3D(Seis3D_State& s) {
 if(functionHeader3D("Compute backend",AppUi::FunctionRole::Simulation)) {
  ImGui::BeginDisabled(s.recording3D||s.sequence3D);
  AppUi::Widgets::Combo(fieldLabel3D("Backend").c_str(),&s.backend3D,"Auto (CUDA / CPU fallback)\0CPU / OpenMP\0CUDA\0");
  AppUi::Widgets::SliderInt(fieldLabel3D("CPU workers").c_str(),&s.workers3D,1,omp_get_num_procs());
  if(functionButton3D("Apply backend",AppUi::FunctionRole::Simulation,ImVec2(-1,0))) performAction3D(s,[&]{initializeSession3D(s,false);});
  ImGui::EndDisabled();
  if(s.ready3D) AppUi::Widgets::Text("%s: %s",AppUi::text("Current"),s.gpu3D.cpu3D?"CPU / OpenMP":"CUDA");
  if(!s.backendStatus3D.empty()) AppUi::Widgets::TextWrapped("%s",s.backendStatus3D.c_str());
 }
 if(functionHeader3D("Time step cost",AppUi::FunctionRole::Simulation)) {
  AppUi::Widgets::Text("%.3f ms / batch | %d steps / frame",s.computeMs3D,s.stepsPerFrame3D);
  AppUi::Widgets::TextWrapped("Batch wall time includes receiver sampling and synchronization; display and export are excluded.");
 }
 if(functionHeader3D("CPU / CUDA efficiency comparison",AppUi::FunctionRole::Simulation)) {
  AppUi::Widgets::TextWrapped("Standard 32 cubed comparison: identical material recipe, source, spacing and dt; CPML width 8. Four warm-up steps and twenty timed steps. Live data is retained.");
  ImGui::BeginDisabled(s.running3D||s.recording3D||s.sequence3D);
  if(functionButton3D("Run CPU / CUDA comparison",AppUi::FunctionRole::Simulation,ImVec2(-1,0)))
   performAction3D(s,[&]{s.benchmarkResult3D=compareBackends3D(s.context3D,s.workers3D);});
  ImGui::EndDisabled();
  if(!s.benchmarkResult3D.empty()) AppUi::Widgets::TextWrapped("%s",s.benchmarkResult3D.c_str());
 }
}

// Active values and staged edits stay visible together, independent of the control tabs.
void drawCurrentParameters3D(Seis3D_State& s) {
    const auto& c=s.context3D;
    if(functionHeader3D("Active / draft parameters",AppUi::FunctionRole::Simulation,ImGuiTreeNodeFlags_DefaultOpen)) {
    if(ImGui::BeginTable("CurrentDraft3D",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(AppUi::text("Parameter"));ImGui::TableSetupColumn(AppUi::text("Current"));ImGui::TableSetupColumn(AppUi::text("Draft"));ImGui::TableHeadersRow();
        auto row=[](const char* label,double live,double draft) {
            ImGui::TableNextRow();ImGui::TableNextColumn();AppUi::Widgets::TextUnformatted(label);
            ImGui::TableNextColumn();ImGui::Text("%.4g",live);ImGui::TableNextColumn();
            if(live!=draft) ImGui::PushStyleColor(ImGuiCol_Text,AppUi::functionColor(AppUi::FunctionRole::Warning));
            ImGui::Text("%.4g",draft);if(live!=draft) ImGui::PopStyleColor();
        };
        const auto& e=s.editContext3D;
        row("NX",c.nx3D,e.nx3D);row("NY",c.ny3D,e.ny3D);row("NZ",c.nz3D,e.nz3D);row("dx (m)",c.spacing3D,e.spacing3D);
        row("f0 (Hz)",c.frequency3D,e.frequency3D);row("CPML",c.pml3D,e.pml3D);row("Vp (m/s)",c.vp3D,e.vp3D);row("Vs (m/s)",c.vs3D,e.vs3D);
        row("rho",c.density3D,e.density3D);row("CPU threads",s.gpu3D.workers3D,s.workers3D);
        ImGui::EndTable();
    }
    AppUi::Widgets::SeparatorText("Active simulation parameters");
    if(!s.ready3D) AppUi::Widgets::TextWrapped("Initialize 3D to activate the staged parameters.");
    else {
        AppUi::Widgets::Text("Grid %d x %d x %d | %.1f m",c.nx3D,c.ny3D,c.nz3D,c.spacing3D);
        AppUi::Widgets::Text("Solver dt: %.6f ms",double(c.dt3D)*1000);
        AppUi::Widgets::Text("Step %llu | Time %.6f s",static_cast<unsigned long long>(s.step3D),s.step3D*double(c.dt3D));
        AppUi::Widgets::TextWrapped("Live boundary: %s",boundaryName3D(c));
        AppUi::Widgets::Text("CPML: %d | Vp max: %.1f m/s",c.reflecting3D?0:c.pml3D,c.maxVp3D);
        AppUi::Widgets::Text("Vp / Vs: %.1f / %.1f m/s",c.vp3D,c.vs3D);
        AppUi::Widgets::Text("Density: %.1f kg/m3",c.density3D);
        AppUi::Widgets::Text("Active frequency: %.2f Hz | delay %.3f ms",c.frequency3D,1200/c.frequency3D);
        AppUi::Widgets::Text("Source XYZ [%d,%d,%d]",s.source3D.x3D,s.source3D.y3D,s.source3D.z3D);
        AppUi::Widgets::Text("Receivers: %d",int(s.receivers3D.size()));
        try {
            AppUi::Widgets::Text("Next capture interval: %.3f ms",receiverSamplingIntervalUs3D(s,c.dt3D)/1000.0);
            AppUi::Widgets::Text("Samples per shot: %d",s.targetSamples3D);
        } catch(const std::exception& e) {AppUi::Widgets::TextWrapped("%s",e.what());}
        if(s.gather3D.sampleCount3D>0) {
            AppUi::Widgets::Text("Captured interval: %.3f ms",s.gather3D.sampleIntervalUs3D/1000.0);
            AppUi::Widgets::Text("Captured samples: %d / %d",s.gather3D.sampleCount3D,s.gather3D.targetSamples3D);
        }
    }
    }
    if(functionHeader3D("Acquisition Settings",AppUi::FunctionRole::Acquisition,ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(s.recording3D||s.sequence3D);
    drawSamplingControls3D(s);
    ImGui::EndDisabled();
    }
    if(functionHeader3D("Update simulation parameters",AppUi::FunctionRole::Model,ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(s.recording3D||s.sequence3D);
    auto& edit=s.editContext3D;
    int sizes[]={edit.nx3D,edit.ny3D,edit.nz3D};
    if(AppUi::Widgets::InputInt3(fieldLabel3D("Nodes X/Y/Z").c_str(),sizes)) {edit.nx3D=sizes[0];edit.ny3D=sizes[1];edit.nz3D=sizes[2];}
    AppUi::Widgets::InputFloat(fieldLabel3D("Grid spacing (m)").c_str(),&edit.spacing3D,1,10,"%.3f");
    drawTimeStepControls3D(s);
    AppUi::Widgets::InputFloat(fieldLabel3D("Ricker f0 (Hz)").c_str(),&edit.frequency3D,1,10,"%.2f");
    AppUi::Widgets::InputFloat(fieldLabel3D("Vp (m/s)").c_str(),&edit.vp3D,100,1000,"%.1f");
    AppUi::Widgets::InputFloat(fieldLabel3D("Vs (m/s)").c_str(),&edit.vs3D,100,1000,"%.1f");
    AppUi::Widgets::InputFloat(fieldLabel3D("Density (kg/m3)").c_str(),&edit.density3D,100,1000,"%.1f");
    AppUi::Widgets::TextWrapped("Model / boundary edits take effect after Rebuild 3D session.");
    if(functionButton3D("Apply parameters + rebuild",AppUi::FunctionRole::Model,ImVec2(-1,0))) performAction3D(s,[&]{initializeSession3D(s,false);});
    ImGui::EndDisabled();
    }
}

// Stable dock names keep saved layouts compatible across mode switches and application restarts.
static constexpr const char* panelNames3D[]={"Simulation controls###Seis3D.Controls","Display / materials###Seis3D.Display",
    "Inline X-Z###Seis3D.Inline","Crossline Y-Z###Seis3D.Crossline","Depth X-Y###Seis3D.Depth",
    "Spatial volume###Seis3D.Volume","Receiver traces###Seis3D.Traces","Current parameters###Seis3D.Parameters"};

// Seed the dock tree only on first use or explicit preset/reset; dragging otherwise owns the layout.
void buildDockLayout3D(Seis3D_State& s,ImGuiID dockId,ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId,ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId,size);
    ImGuiID center=dockId;
    ImGuiID controls=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,(std::clamp)(ImGui::GetFontSize()*23.0f/size.x,.22f,.38f),nullptr,&center);
    ImGui::DockBuilderDockWindow(panelNames3D[0],controls);
    ImGui::DockBuilderDockWindow(panelNames3D[1],controls);
    ImGui::DockBuilderDockWindow(panelNames3D[7],controls);
    if(s.layoutPreset3D==2) {
        ImGui::DockBuilderDockWindow(panelNames3D[6],controls);
        for(int i=2;i<6;++i) ImGui::DockBuilderDockWindow(panelNames3D[i],center);
    } else if(s.layoutPreset3D==3) {
        ImGuiID views=ImGui::DockBuilderSplitNode(center,ImGuiDir_Up,.30f,nullptr,&center);
        for(int i=2;i<6;++i) ImGui::DockBuilderDockWindow(panelNames3D[i],views);
        ImGui::DockBuilderDockWindow(panelNames3D[6],center);
    } else {
        ImGuiID traces=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.25f,nullptr,&center);
        ImGui::DockBuilderDockWindow(panelNames3D[6],traces);
        ImGuiID left=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,s.layoutPreset3D==1?.38f:.5f,nullptr,&center);
        if(s.layoutPreset3D==1) {
            for(int i=2;i<5;++i) ImGui::DockBuilderDockWindow(panelNames3D[i],left);
            ImGui::DockBuilderDockWindow(panelNames3D[5],center);
        } else {
            ImGuiID leftBottom=ImGui::DockBuilderSplitNode(left,ImGuiDir_Down,.5f,nullptr,&left);
            ImGuiID rightBottom=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.5f,nullptr,&center);
            ImGui::DockBuilderDockWindow(panelNames3D[2],left);
            ImGui::DockBuilderDockWindow(panelNames3D[3],center);
            ImGui::DockBuilderDockWindow(panelNames3D[4],leftBottom);
            ImGui::DockBuilderDockWindow(panelNames3D[5],rightBottom);
        }
    }
    ImGui::DockBuilderFinish(dockId);
    std::fill_n(s.panelsOpen3D,8,true);s.resetLayout3D=false;
}

// Use the same state transitions as the controls; display shortcuts never apply preset defaults.
bool handleWorkspaceShortcuts3D(Seis3D_State& s) {
    if(!AppUi::workspaceShortcutsAvailable(true)) return false;
    const bool reverse=ImGui::GetIO().KeyShift;
    if(ImGui::IsKeyPressed(ImGuiKey_Q,false))
        s.waveStyle3D=AppUi::cycleDisplaySelection(s.waveStyle3D,waveStyleCount3D,reverse);
    if(ImGui::IsKeyPressed(ImGuiKey_W,false))
        selectWaveComponent3D(s,AppUi::cycleDisplaySelection(s.component3D,waveComponentCount3D,reverse));
    if(ImGui::IsKeyPressed(ImGuiKey_E,false))
        s.modelStyle3D=AppUi::cycleDisplaySelection(s.modelStyle3D,modelStyleCount3D,reverse);
    if(reverse) return false;
    if(ImGui::IsKeyPressed(ImGuiKey_Space,false)&&s.ready3D) performAction3D(s,[&]{toggleSimulation3D(s);});
    if(ImGui::IsKeyPressed(ImGuiKey_N,false)&&s.ready3D) {s.running3D=false;s.singleStep3D=true;}
    if(ImGui::IsKeyPressed(ImGuiKey_C,false)&&s.ready3D&&!s.recording3D&&!s.sequence3D)
        performAction3D(s,[&]{restartSimulation3D(s,false);});
    if(ImGui::IsKeyPressed(ImGuiKey_R,false)) {resetSliceViews3D(s);s.camera3D=VolumeCamera3D{};}
    if(ImGui::IsKeyPressed(ImGuiKey_G,false)) s.showGrid3D=!s.showGrid3D;
    if(ImGui::IsKeyPressed(ImGuiKey_B,false)) {s.showBoundary3D=!s.showBoundary3D;resetSliceViews3D(s);s.dirty3D=true;}
    if(ImGui::IsKeyPressed(ImGuiKey_V,false)) {s.showVolume3D=!s.showVolume3D;s.dirty3D=true;}
    return ImGui::IsKeyPressed(ImGuiKey_F1,false);
}

// Keep keyboard commands discoverable in both languages without adding permanent panel clutter.
void drawShortcutHelp3D() {
    const auto size=ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2((std::min)(650.0f,(std::max)(240.0f,size.x-24)),0),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0,0),ImVec2((std::max)(240.0f,size.x-24),(std::max)(160.0f,size.y-24)));
    if(!ImGui::BeginPopup("KeyboardShortcuts3D")) return;
    AppUi::Widgets::SeparatorText("3D keyboard shortcuts");
    const char* rows[][2]={
        {"Q / Shift+Q","Next / previous wave palette"},
        {"W / Shift+W","Next / previous wave component"},
        {"E / Shift+E","Next / previous model palette"},
        {"Space","Run / pause"},{"N","Advance one solver step"},
        {"C","Reset wavefield and pause"},{"R","Fit all slices and reset camera"},
        {"G","Show / hide physical grid"},{"B","Show / hide boundary layers"},
        {"V","Enable / disable volume rendering"},{"F1","Open shortcut help"},
        {"Tab","Return to Home"}};
    if(ImGui::BeginTable("ShortcutTable3D",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp,
        ImVec2((std::max)(200.0f,(std::min)(610.0f,size.x-64)),0))) {
        ImGui::TableSetupColumn("Keys",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*9);
        ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch,1);
        for(const auto& row:rows) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(row[0]);
            ImGui::TableNextColumn();AppUi::Widgets::TextWrapped("%s",row[1]);
        }
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*34);
    AppUi::Widgets::TextWrapped("Shortcuts are suspended while editing controls or using popups. Ctrl / Alt combinations are reserved. Color and component switches retain gains, opacity and raw data.");
    ImGui::PopTextWrapPos();
    if(AppUi::Widgets::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Compose an isolated, persistent dockspace with a fixed toolbar and compact physical status bar.
bool renderScreen3D(Seis3D_State& s,int width,int height) {
    (void)width;(void)height;
    ImGui::PushFont(nullptr,ImGui::GetFontSize()*(std::clamp)(s.fontScale3D,.85f,1.25f));
    s.homeRequested3D=false;
    const bool shortcutHelpRequested=handleWorkspaceShortcuts3D(s);
    if(ImGui::GetTime()-s.memoryRefreshTime3D>1) {
        s.memoryRefreshTime3D=ImGui::GetTime();
        if(cudaMemGetInfo(&s.freeVram3D,&s.totalVram3D)!=cudaSuccess) s.freeVram3D=s.totalVram3D=0;
    }
    performAction3D(s,[&]{advanceSimulation3D(s);});
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    AppUi::Widgets::Begin("Seis3D Dock Host",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);
    if(functionButton3D("Home",AppUi::FunctionRole::Navigation)) s.homeRequested3D=true;
    continueToolbar3D("SEIS3D");AppUi::Widgets::TextColored(AppUi::palette().accent,"SEIS3D");
    if(!s.ready3D) {continueToolbar3D("Initialize 3D");if(functionButton3D("Initialize 3D",AppUi::FunctionRole::Model)) performAction3D(s,[&]{initializeSession3D(s,false);});}
    continueToolbar3D("Replay");ImGui::BeginDisabled(!s.ready3D);
    const bool completed=!s.recording3D&&s.step3D>=std::uint64_t((std::clamp)(s.maximumSteps3D,1,1000000));
    if(functionButton3D(s.running3D?"Pause":completed?"Replay":"Run",s.running3D?AppUi::FunctionRole::Warning:AppUi::FunctionRole::Simulation)) performAction3D(s,[&]{toggleSimulation3D(s);});
    continueToolbar3D("Step");if(functionButton3D("Step",AppUi::FunctionRole::Simulation)) {s.running3D=false;s.singleStep3D=true;}
    continueToolbar3D("Reset fields");ImGui::BeginDisabled(s.recording3D);
    if(functionButton3D("Reset fields",AppUi::FunctionRole::Danger)) performAction3D(s,[&]{restartSimulation3D(s,false);});
    continueToolbar3D("Fire + capture");if(functionButton3D("Fire + capture",AppUi::FunctionRole::Acquisition)) performAction3D(s,[&]{s.sequence3D=false;s.traceView3D=2;startGather3D(s,1);});
    ImGui::EndDisabled();ImGui::EndDisabled();continueToolbar3D("Workspace");
    if(functionButton3D("Workspace",AppUi::FunctionRole::Display)) ImGui::OpenPopup("WorkspaceMenu3D");
    if(ImGui::BeginPopup("WorkspaceMenu3D")) {
        if(AppUi::Widgets::MenuItem("Four views",nullptr,s.layoutPreset3D==0)) {s.layoutPreset3D=0;s.resetLayout3D=true;}
        if(AppUi::Widgets::MenuItem("Volume focus",nullptr,s.layoutPreset3D==1)) {s.layoutPreset3D=1;s.resetLayout3D=true;}
        if(AppUi::Widgets::MenuItem("Compact / tabbed",nullptr,s.layoutPreset3D==2)) {s.layoutPreset3D=2;s.resetLayout3D=true;}
        if(AppUi::Widgets::MenuItem("Receiver focus",nullptr,s.layoutPreset3D==3)) {s.layoutPreset3D=3;s.resetLayout3D=true;}
        if(AppUi::Widgets::MenuItem("Restore current preset")) s.resetLayout3D=true;
        if(AppUi::Widgets::MenuItem("Show boundary layers / walls",nullptr,&s.showBoundary3D)) {resetSliceViews3D(s);s.dirty3D=true;}
        AppUi::Widgets::SliderFloat("Text size",&s.fontScale3D,.85f,1.25f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
        if(AppUi::Widgets::BeginMenu("Visual preset")) {
            const char* presets[]={"Wavefront contrast","Signed seismic","Geological layers","CUDA glow","Paper scientific"};
            for(int i=0;i<5;++i) if(AppUi::Widgets::MenuItem(presets[i],nullptr,s.visualPreset3D==i)) applyVisualPreset3D(s,i);
            ImGui::EndMenu();
        }
        AppUi::drawThemeMenu();AppUi::drawLanguageMenu();
        ImGui::Separator();
        const char* labels[]={"Simulation controls","Display / materials","Inline","Crossline","Depth","Volume","Receiver traces","Current parameters"};
        for(int i=0;i<8;++i) AppUi::Widgets::MenuItem(labels[i],nullptr,&s.panelsOpen3D[i]);
        ImGui::EndPopup();
    }
    continueToolbar3D("Reset views");if(functionButton3D("Reset views",AppUi::FunctionRole::Display)) {resetSliceViews3D(s);s.camera3D=VolumeCamera3D{};}
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("Fit all three slices and reset the orbit camera (R). Keeps wavefield and slice positions.");
    continueToolbar3D("Shortcuts (F1)");
    if(functionButton3D("Shortcuts (F1)",AppUi::FunctionRole::Navigation)||shortcutHelpRequested) ImGui::OpenPopup("KeyboardShortcuts3D");
    drawShortcutHelp3D();
    continueToolbar3D("PAUSED | 000 FPS | 000.00 ms/batch");
    AppUi::Widgets::TextColored(AppUi::functionColor(s.recording3D?AppUi::FunctionRole::Acquisition:s.running3D?AppUi::FunctionRole::Simulation:AppUi::FunctionRole::Warning),
        "%s",s.recording3D?"CAPTURING":s.running3D?"RUNNING":s.ready3D?"PAUSED":"SETUP");
    ImGui::SameLine();AppUi::Widgets::TextDisabled("| %.0f FPS | %.2f ms/batch",ImGui::GetIO().Framerate,s.computeMs3D);
    AppUi::Widgets::TextWrapped("t %.4f s | step %llu | Inline Y %d / Crossline X %d | VRAM %.2f / %.2f GiB",
        s.step3D*double(s.context3D.dt3D),static_cast<unsigned long long>(s.step3D),s.slice3D[1],s.slice3D[0],
        (s.totalVram3D-s.freeVram3D)/gibibyte3D,s.totalVram3D/gibibyte3D);
    AppUi::Widgets::TextColored(AppUi::palette().accent,"%s | Boundary display: %s",boundaryName3D(s.ready3D?s.context3D:s.editContext3D),s.showBoundary3D?"ON":"OFF");
    ImGuiWindowClass windowClass;windowClass.ClassId=ImGui::GetID("Seis3D.IsolatedWindows");windowClass.DockingAllowUnclassed=false;
    const ImGuiID dockId=ImGui::GetID("Seis3D.Dockspace.v4");
    const ImVec2 dockSize((std::max)(1.0f,ImGui::GetContentRegionAvail().x),
        (std::max)(60.0f,ImGui::GetContentRegionAvail().y-ImGui::GetTextLineHeightWithSpacing()*2-10));
    if(!ImGui::DockBuilderGetNode(dockId)||s.resetLayout3D) buildDockLayout3D(s,dockId,dockSize);
    ImGui::DockSpace(dockId,dockSize,ImGuiDockNodeFlags_None,&windowClass);
    if(s.ready3D&&s.hoverValid3D) {
        const auto q=s.hoverPoint3D;const float h=s.context3D.spacing3D;
        AppUi::Widgets::Text("XYZ [%d,%d,%d]  (%.0f,%.0f,%.0f) m | Vp %.0f / Vs %.0f m/s | rho %.0f kg/m3",
            q.x3D,q.y3D,q.z3D,q.x3D*h,q.y3D*h,q.z3D*h,s.hoverMaterial3D[0],s.hoverMaterial3D[1],s.hoverMaterial3D[2]);
    } else if(s.ready3D) {
        const float* range=s.materialRangeAuto3D?s.materialRange3D:s.manualMaterialRange3D;
        AppUi::Widgets::TextDisabled("%s %.0f .. %.0f %s | Wave max %.2e | Hover: probe / drag tabs: dock",
            s.materialComponent3D==5?"Vp":s.materialComponent3D==6?"Vs":"Density",range[0],range[1],
            s.materialComponent3D==7?"kg/m3":"m/s",s.viewMaximum3D);
    } else AppUi::Widgets::TextDisabled("Initialize a model in Simulation | Workspace: layouts and panels | Space: run/pause");
    AppUi::Widgets::TextColored(s.messageIsError3D?AppUi::functionColor(AppUi::FunctionRole::Danger):AppUi::palette().muted,"%s",s.message3D.c_str());
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("%s",s.message3D.c_str());
    ImGui::End();
    for(int panel=0;panel<8;++panel) {
        // Apply controls before rendering, and render textures before submitting ImGui images.
        if(panel==2) performAction3D(s,[&]{updateGraphics3D(s);});
        if(!s.panelsOpen3D[panel]) continue;
        ImGui::SetNextWindowClass(&windowClass);
        ImGui::SetNextWindowSize(ImVec2(400,320),ImGuiCond_FirstUseEver);
        bool visible=false;
        {
            const AppUi::FunctionRole roles[]={AppUi::FunctionRole::Simulation,AppUi::FunctionRole::Display,
                AppUi::FunctionRole::Display,AppUi::FunctionRole::Display,AppUi::FunctionRole::Display,
                AppUi::FunctionRole::Display,AppUi::FunctionRole::Acquisition,AppUi::FunctionRole::Simulation};
            AppUi::FunctionStyleScope panelColors(roles[panel]);
            visible=AppUi::Widgets::Begin(panelNames3D[panel],&s.panelsOpen3D[panel]);
        }
        if(visible) {
            ImGui::PushItemWidth((std::max)(80.0f,ImGui::GetContentRegionAvail().x*.55f));
            if(panel<=1||panel==7) drawSectionTools3D();
            if(panel==0) drawBackendControls3D(s);
            if(panel==0 && ImGui::BeginTabBar("ControlTabs3D",ImGuiTabBarFlags_FittingPolicyScroll)) {
                if(functionTab3D("Simulation",AppUi::FunctionRole::Simulation)) {drawSimulationControls3D(s);ImGui::EndTabItem();}
                if(functionTab3D("Source",AppUi::FunctionRole::Source)) {drawSourceControls3D(s);ImGui::EndTabItem();}
                if(functionTab3D("Model I/O",AppUi::FunctionRole::Model)) {drawModelControls3D(s);ImGui::EndTabItem();}
                if(functionTab3D("Acquisition",AppUi::FunctionRole::Acquisition)) {AppUi::FunctionStyleScope acquisitionColors(AppUi::FunctionRole::Acquisition);drawAcquisitionControls3D(s);ImGui::EndTabItem();}
                ImGui::EndTabBar();
            }
            if(panel==1) {drawViewControls3D(s);drawLegend3D(s);}
            if(panel>=2&&panel<=4) drawSlicePanel3D(s,panel-2);
            if(panel==5) drawVolumePanel3D(s);
            if(panel==6) drawTracePreview3D(s);
            if(panel==7) drawCurrentParameters3D(s);
            ImGui::PopItemWidth();
        }
        ImGui::End();
    }
    ImGui::PopFont();
    return s.homeRequested3D;
}

} // namespace Seis3D
