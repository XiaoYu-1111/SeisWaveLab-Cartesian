#pragma once
#include "imgui_internal.h"
#include "../src/Seis3D/Visual3D.h"
#include "RecordVisuals.h"
#include <chrono>
#include <fstream>
#include <iomanip>

namespace Seis3D::Documentation3D {
// Drive only an explicitly requested documentation session through normal solver and rendering APIs.
struct WorkflowCapture3D {
    int stage3D=0, frames3D=0;
    std::filesystem::path root3D, data3D;
    std::ofstream manifest3D;
    const char* tab3D="Simulation";
    bool displayPanel3D=false;
    std::vector<const char*> openGroups3D;
    inline static constexpr const char* names3D[]={
        "01-home","02-model","03-stability","04-source","05-model-io","06-receivers",
        "07-acquisition-settings","08-wavefield","09-style-coolwarm","10-style-bipolar",
        "11-style-viridis","12-style-turbo","13-style-inferno","14-style-cuda",
        "15-component-magnitude","16-volume-settings","17-boundary-visible","18-capture-progress",
        "19-record-all","20-record-row","21-record-agc","22-record-density","23-single-trace",
        "24-export","25-import","26-agc-options","27-shortcuts","28-model-fault",
        "29-home-english","30-operating-manual","31-boundary-settings","32-all-absorbing","33-all-reflecting"};

    // Keep images at native framebuffer size and exports in a unique outer-out documentation directory.
    void initialize3D(Seis3D_State& s) {
        root3D=getExecutableDirectory().parent_path().parent_path()/"screenshots/seis3d-full";
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        data3D=root3D/"data"/("session-"+std::to_string(stamp));
        std::filesystem::create_directories(data3D);
        manifest3D.open(root3D/"capture-manifest.csv",std::ios::trunc);
        manifest3D<<"image,width,height,nx,ny,nz,dt_us,step,receivers,captured_samples,wave_component,wave_palette,model_palette,layout,agc,slice_zoom\n";
        ImGui::ClearIniSettings();ImGui::GetIO().IniFilename=nullptr;
        AppUi::setTheme(AppUi::ThemeId::PaperSand,false);AppUi::setLanguage(AppUi::Language::Chinese,false);
        s.editContext3D.nx3D=s.editContext3D.ny3D=s.editContext3D.nz3D=200;
        // Preserve the documented 150-channel screenshot scenario independently from application defaults.
        s.survey3D.receiverCountX3D=50;s.survey3D.receiverCountY3D=3;
        s.editContext3D.manualTimeStep3D=true;s.editContext3D.requestedDtMs3D=.5f;
        s.targetSamples3D=1000;s.samplingStride3D=2;s.stepsPerFrame3D=8;s.fontScale3D=1;
        s.resetLayout3D=true;
        std::snprintf(s.exportDirectory3D,sizeof(s.exportDirectory3D),"%s",pathToUtf8(data3D).c_str());
        std::snprintf(s.exportPrefix3D,sizeof(s.exportPrefix3D),"survey");s.exportSessionFolder3D=false;
        std::snprintf(s.modelExportPath3D,sizeof(s.modelExportPath3D),"%s",pathToUtf8(data3D/"two-layer.vti").c_str());
        std::snprintf(s.modelPath3D,sizeof(s.modelPath3D),"%s",s.modelExportPath3D);
    }

    // Select the actual control tab and its folds using window/tab-scoped identities, never resizing pictures.
    void selectPanel3D() {
        const char* windowName=displayPanel3D?"Display / materials###Seis3D.Display":"Simulation controls###Seis3D.Controls";
        auto* window=ImGui::FindWindowByName(windowName);if(!window) return;
        if(frames3D<3) ImGui::SetWindowFocus(windowName);
        ImGuiID seed=window->ID;
        if(!displayPanel3D) {
            const auto barId=ImHashStr("ControlTabs3D",0,window->ID);
            if(auto* bar=GImGui->TabBars.GetByKey(barId)) {
                seed=ImHashStr(AppUi::label(tab3D),0,barId);bar->NextSelectedTabId=seed;
            }
        }
        const char* groups[]={"GRID / RESOLUTION","BOUNDARY CONDITIONS","MODEL / MATERIAL","Secondary / bottom material",
            "Model geometry (domain fractions)","TIME / STABILITY","MEMORY / STABILITY","RUN / ITERATION",
            "SOURCE WAVELET","SOURCE POSITION","SOURCE MECHANISM","MODEL FILE DETAILS","SURFACE RECEIVER MESH",
            "WORLD COORDINATES / CMP","RECORDING / EXPORT","MULTI-LINE SOURCE SPREAD","APPEARANCE / LANGUAGE",
            "WAVEFIELD / GAIN","AMPLITUDE REFERENCE","WAVEFIELD TRANSFER","GEOLOGICAL MATERIAL",
            "GRID / BOUNDARY OVERLAY","VOLUME / PERFORMANCE","VIEW NAVIGATION","COLOR / PHYSICAL SCALE"};
        for(const auto* group:groups) {
            const bool opened=std::find(openGroups3D.begin(),openGroups3D.end(),group)!=openGroups3D.end();
            window->StateStorage.SetBool(ImHashStr(AppUi::label(group),0,seed),opened);
        }
        window->Scroll.y=0;window->ScrollTarget.y=0;
    }

    // Advance a real physical workload once, then hold identical fields for all palette comparisons.
    void beforeFrame3D(Seis3D_State& s,SimState& app) {
        using namespace Detail3D;
        if(frames3D==0) {
            ImGui::ClosePopupsOverWindow(nullptr,false);
            displayPanel3D=false;tab3D="Simulation";openGroups3D={};
            switch(stage3D) {
            case 1:
                initializeSession3D(s,false);applyVisualPreset3D(s,4);resetDisplayParameters3D(s);
                selectWaveComponent3D(s,0);app.currentScreen=AppScreen::SeisSim_3D;
                s.displayMode3D=2;openGroups3D={"GRID / RESOLUTION","MODEL / MATERIAL","Secondary / bottom material"};break;
            case 2:openGroups3D={"TIME / STABILITY","MEMORY / STABILITY"};break;
            case 3:tab3D="Source";openGroups3D={"SOURCE WAVELET","SOURCE POSITION","SOURCE MECHANISM"};break;
            case 4:tab3D="Model I/O";exportModel3D(s);openGroups3D={"MODEL FILE DETAILS"};break;
            case 5:tab3D="Acquisition";openGroups3D={"SURFACE RECEIVER MESH"};break;
            case 6:tab3D="Acquisition";openGroups3D={"RECORDING / EXPORT","WORLD COORDINATES / CMP"};break;
            case 7:s.displayMode3D=0;s.slice3D[2]=s.source3D.z3D+20;s.dirty3D=true;
                s.traceView3D=1;s.running3D=true;openGroups3D={"RUN / ITERATION"};break;
            case 8:case 9:case 10:case 11:case 12:case 13:
                displayPanel3D=true;openGroups3D={"WAVEFIELD / GAIN","GEOLOGICAL MATERIAL","COLOR / PHYSICAL SCALE"};
                s.waveStyle3D=stage3D==8?1:stage3D==9?0:stage3D==10?6:stage3D==11?7:stage3D==12?4:13;break;
            case 14:displayPanel3D=true;s.waveStyle3D=1;selectWaveComponent3D(s,3);openGroups3D={"WAVEFIELD / GAIN","COLOR / PHYSICAL SCALE"};break;
            case 15:displayPanel3D=true;openGroups3D={"VOLUME / PERFORMANCE"};break;
            case 16:displayPanel3D=true;s.showBoundary3D=true;resetSliceViews3D(s);s.dirty3D=true;openGroups3D={"GRID / BOUNDARY OVERLAY"};break;
            case 17:s.showBoundary3D=false;resetSliceViews3D(s);selectWaveComponent3D(s,0);
                s.traceView3D=2;startGather3D(s,1);tab3D="Acquisition";openGroups3D={"RECORDING / EXPORT"};break;
            case 18:s.running3D=true;s.layoutPreset3D=3;s.resetLayout3D=true;s.traceDisplay3D=1;
                s.traceAgc3D.enabled3D=false;s.traceFitRequest3D=true;tab3D="Acquisition";openGroups3D={"RECORDING / EXPORT"};break;
            case 19:s.traceDisplay3D=2;s.traceRow3D=1;s.traceFitRequest3D=true;tab3D="Acquisition";openGroups3D={"SURFACE RECEIVER MESH"};break;
            case 20:s.traceAgc3D.enabled3D=true;s.traceFitRequest3D=true;tab3D="Acquisition";break;
            case 21:s.traceStyle3D=2;Seismic::applyRecordStyle(2,s.traceShowWiggle3D,s.traceShowHeatmap3D,s.tracePositiveFill3D,s.traceSignedFill3D);s.traceFitRequest3D=true;break;
            case 22:s.traceDisplay3D=0;s.traceFitRequest3D=true;break;
            case 23:exportGather3D(s,data3D);s.layoutPreset3D=0;s.resetLayout3D=true;tab3D="Acquisition";openGroups3D={"RECORDING / EXPORT"};break;
            case 24: {
                const auto path=data3D/"survey_0001_vz.sgy";
                if(!importShotRecord3D(s,path)) throw std::runtime_error(s.message3D);
                std::snprintf(s.shotImportPath3D,sizeof(s.shotImportPath3D),"%s",pathToUtf8(path).c_str());
                s.layoutPreset3D=3;s.resetLayout3D=true;s.traceDisplay3D=2;s.traceRow3D=1;s.traceFitRequest3D=true;tab3D="Acquisition";break;
            }
            case 25:tab3D="Acquisition";break;
            case 26:displayPanel3D=true;break;
            case 27:s.layoutPreset3D=0;s.resetLayout3D=true;applyModelPreset3D(s.editContext3D,ModelKind3D::Fault);
                initializeSession3D(s,false);s.displayMode3D=2;s.traceView3D=1;s.traceDisplay3D=0;
                openGroups3D={"MODEL / MATERIAL","Model geometry (domain fractions)"};break;
            case 28:app.currentScreen=AppScreen::Intro0;AppUi::setLanguage(AppUi::Language::English,false);break;
            case 29:app.currentScreen=AppScreen::UserManual;AppUi::setLanguage(AppUi::Language::Chinese,false);break;
            case 30:app.currentScreen=AppScreen::SeisSim_3D;applyModelPreset3D(s.editContext3D,ModelKind3D::TwoLayer);
                initializeSession3D(s,false);s.traceView3D=1;s.displayMode3D=2;s.resetLayout3D=true;
                openGroups3D={"BOUNDARY CONDITIONS","MEMORY / STABILITY"};break;
            case 31:s.editContext3D.freeSurface3D=false;initializeSession3D(s,false);
                s.showBoundary3D=true;resetSliceViews3D(s);openGroups3D={"BOUNDARY CONDITIONS","MEMORY / STABILITY"};break;
            case 32:s.editContext3D.reflecting3D=true;initializeSession3D(s,false);
                openGroups3D={"BOUNDARY CONDITIONS","MEMORY / STABILITY"};break;
            default:break;
            }
        }
        if(stage3D>=7&&stage3D<=16&&s.step3D>=480) s.running3D=false;
        if(stage3D==17&&s.step3D>=960) s.running3D=false;
        if(stage3D==18&&!s.recording3D) s.stepsPerFrame3D=2;
        if(app.currentScreen==AppScreen::SeisSim_3D) selectPanel3D();
        if(stage3D==25&&frames3D==3) {
            if(auto* window=ImGui::FindWindowByName("Receiver traces###Seis3D.Traces")) {
                auto* previous=GImGui->CurrentWindow;GImGui->CurrentWindow=window;
                window->IDStack.push_back(ImHashStr("TraceRowControls3D",0,window->ID));
                ImGui::OpenPopup("TraceOptions3D");window->IDStack.pop_back();GImGui->CurrentWindow=previous;
            }
        }
        if(stage3D==26&&frames3D==3) {
            if(auto* window=ImGui::FindWindowByName("Seis3D Dock Host")) {
                auto* previous=GImGui->CurrentWindow;GImGui->CurrentWindow=window;
                ImGui::OpenPopup("KeyboardShortcuts3D");GImGui->CurrentWindow=previous;
            }
        }
    }

    // Wait for normal layout settling and requested computation; serialize the actual screenshot conditions.
    bool afterFrame3D(GLFWwindow* window,Seis3D_State& s) {
        if(s.messageIsError3D) throw std::runtime_error(s.message3D);
        ++frames3D;
        if(frames3D<8||(stage3D==7&&(s.step3D<480||s.running3D))
            ||(stage3D==17&&(s.step3D<960||s.running3D))||(stage3D==18&&s.recording3D)) return false;
        const auto path=root3D/(std::string(names3D[stage3D])+".png");
        saveApplicationScreenshot(window,pathToUtf8(path).c_str());
        int width=0,height=0;glfwGetFramebufferSize(window,&width,&height);
        manifest3D<<names3D[stage3D]<<".png,"<<width<<','<<height<<','<<s.context3D.nx3D<<','<<s.context3D.ny3D<<','<<s.context3D.nz3D
            <<','<<std::llround(double(s.context3D.dt3D)*1e6)<<','<<s.step3D<<','<<s.receivers3D.size()<<','<<s.gather3D.sampleCount3D
            <<','<<s.component3D<<','<<s.waveStyle3D<<','<<s.modelStyle3D<<','<<s.layoutPreset3D<<','<<s.traceAgc3D.enabled3D<<",1\n";
        std::cout<<"Documentation capture: "<<names3D[stage3D]<<" ("<<width<<"x"<<height<<")\n";
        ++stage3D;frames3D=0;manifest3D.flush();return stage3D==int(std::size(names3D));
    }
};
} // namespace Seis3D::Documentation3D
