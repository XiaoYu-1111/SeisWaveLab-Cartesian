#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif
#include "SeismicViewer2D.h"
#include "AppWidgets.h"
#include "AppTheme.h"
#include "ResourcePaths.h"
#include "implot.h"
#include "RecordVisuals.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Seis2D {
namespace {
struct WiggleContext2D {const Seismic::TracePreview* preview;double offset,scale;int lobe;};
// Place one processed amplitude about its recorded channel number at the true sample timestamp.
ImPlotPoint wigglePoint2D(int i,void* data) {
    const auto& plot=*static_cast<WiggleContext2D*>(data);const double amplitude=plot.preview->values[i];
    return {plot.offset+(plot.lobe>0?std::max(0.0,amplitude):plot.lobe<0?std::min(0.0,amplitude):amplitude)*plot.scale,plot.preview->times[i]};
}
// Supply the zero-amplitude baseline for positive variable-area shading.
ImPlotPoint baselinePoint2D(int i,void* data) {
    const auto& plot=*static_cast<WiggleContext2D*>(data);return {plot.offset,plot.preview->times[i]};
}
// Decode the editable UTF-8 field into a native Unicode filesystem path.
std::filesystem::path viewerPath2D(const char* text) {
    const auto length=std::strlen(text);std::u8string encoded(length,u8'\0');std::memcpy(encoded.data(),text,length);return std::filesystem::path(encoded);
}
// Browse without changing the working directory; selecting a file does not import it yet.
void browseRecord2D(ViewerState2D& state) {
#ifdef _WIN32
    wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
    dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=L"SEG-Y records\0*.sgy;*.segy\0All files\0*.*\0";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)) {
        const auto path=pathToUtf8(std::filesystem::path(file));
        if(path.size()>=sizeof(state.importPath)) throw std::runtime_error("Selected path or filename exceeds the field length.");
        std::memcpy(state.importPath,path.c_str(),path.size()+1);
    } else if(CommDlgExtendedError()) throw std::runtime_error("Windows could not open the file selector.");
#endif
}
// Use the same independent amplitude and AGC controls as the 3D receiver viewer.
void drawTraceOptions2D(ViewerState2D& state) {
    if(AppUi::Widgets::Button("Trace options")) ImGui::OpenPopup("TraceOptions2D");
    if(!ImGui::BeginPopup("TraceOptions2D")) return;
    ImGui::SetNextItemWidth(260);
    if(AppUi::Widgets::SliderFloat("Trace gain",&state.gain,.1f,20,"%.2fx",ImGuiSliderFlags_AlwaysClamp)&&state.viewMode==1)
        state.fitRequest=true;
    AppUi::Widgets::Checkbox("Automatic gain compensation",&state.agc.enabled);
    if(state.agc.enabled) {
        ImGui::SetNextItemWidth(260);AppUi::Widgets::SliderFloat("AGC window (ms)",&state.agc.windowMs,5,2000,"%.0f ms",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(260);AppUi::Widgets::SliderFloat("AGC strength",&state.agc.strength,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(260);AppUi::Widgets::SliderFloat("Maximum AGC boost",&state.agc.maxBoost,1,200,"%.0fx",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(260);AppUi::Widgets::SliderFloat("Noise floor (percent of peak)",&state.agc.noiseFloorPercent,0,2,"%.3f%%",ImGuiSliderFlags_AlwaysClamp);
        if(AppUi::Widgets::Button("Reset AGC parameters")) state.agc=Seismic::AgcSettings{true};
    }
    AppUi::Widgets::Checkbox("Normalize each trace",&state.normalizeEach);
    AppUi::Widgets::TextWrapped(state.normalizeEach?"Each channel has its own scale; relative amplitudes are not comparable.":"One amplitude scale for the selected channels; relative amplitudes are preserved.");
    if(AppUi::Widgets::Checkbox("Positive variable area",&state.positiveFill)) {state.recordStyle=6;if(!state.positiveFill) state.signedFill=false;}
    AppUi::Widgets::Checkbox("Auto fit traces",&state.autoFit);
    if(AppUi::Widgets::Button("Fit traces now")) state.fitRequest=true;
    AppUi::Widgets::TextWrapped("AGC changes relative amplitudes in the preview. SEG-Y export keeps original samples.");
    ImGui::EndPopup();
}
// Plot independent single-trace time/amplitude axes or a paged receiver section with inverted time.
void drawRecordPlot2D(ViewerState2D& state) {
    const auto& cache=state.cache;const auto& p=AppUi::palette();
    if(cache.previews.empty()) {AppUi::Widgets::TextWrapped("Load a SEG-Y record or open a snapshot of the current 2D capture.");return;}
    ImPlot::PushStyleColor(ImPlotCol_FrameBg,p.background);ImPlot::PushStyleColor(ImPlotCol_PlotBg,p.panel);
    ImPlot::PushStyleColor(ImPlotCol_AxisText,p.text);ImPlot::PushStyleColor(ImPlotCol_AxisGrid,AppUi::color(p.muted,.15f));
    const bool colorbar=state.viewMode==0&&state.showHeatmap&&ImGui::GetContentRegionAvail().x>320;
    const float height=std::max(120.0f,ImGui::GetContentRegionAvail().y);
    const float colorbarWidth=Seismic::recordColorbarWidth();
    const ImVec2 plotSize(std::max(100.0f,ImGui::GetContentRegionAvail().x-(colorbar?colorbarWidth+ImGui::GetStyle().ItemSpacing.x:0)),height);
    Seismic::beginRecordNavigation(state.autoFit,state.fitRequest,plotSize);
    const auto palette=Seismic::recordColormap(state.palette);
    if(ImPlot::BeginPlot("Record###Seis2D.RecordPlot",plotSize,ImPlotFlags_NoTitle|ImPlotFlags_NoLegend)) {
        const bool single=state.viewMode==1,fit=state.fitRequest||state.autoFit;
        ImPlot::SetupAxes(AppUi::text(single?"Time (s)":"Trace Number"),AppUi::text(single?(state.agc.enabled?"AGC display amplitude":"Amplitude"):"Time (s)"),0,single?0:ImPlotAxisFlags_Invert);
        if(fit) {
            const double peak=std::max(cache.peak,1e-30),margin=.5*state.gain+.2;
            ImPlot::SetupAxisLimits(ImAxis_X1,single?cache.timeMin:cache.first+1-margin,single?cache.timeMax:cache.first+cache.count+margin,ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1,single?-peak*1.1/state.gain:cache.timeMin,single?peak*1.1/state.gain:cache.timeMax,ImPlotCond_Always);
        }
        if(single) {
            const auto& preview=cache.previews.front();ImPlot::SetNextLineStyle(Seismic::recordPolarityColor(state.palette,true),1.5f);
            if(state.positiveFill) {
                Seismic::SingleTracePlot plot{&preview.times,&preview.values,1};
                ImPlot::SetNextFillStyle(Seismic::recordPolarityColor(state.palette,true),.25f);
                ImPlot::PlotShadedG("##SinglePositive",Seismic::singleTracePoint,&plot,Seismic::singleTraceBaseline,&plot,int(preview.values.size()));
                if(state.signedFill) {plot.lobe=-1;ImPlot::SetNextFillStyle(Seismic::recordPolarityColor(state.palette,false),.25f);
                    ImPlot::PlotShadedG("##SingleNegative",Seismic::singleTracePoint,&plot,Seismic::singleTraceBaseline,&plot,int(preview.values.size()));}
                ImPlot::SetNextLineStyle(Seismic::recordPolarityColor(state.palette,true),1.5f);
            }
            ImPlot::PlotLine("Trace",preview.times.data(),preview.values.data(),int(preview.values.size()));
        } else {
            if(state.showHeatmap) {
                ImPlot::PushColormap(Seismic::recordColormap(state.palette,state.densityOpacity));
                ImPlot::PlotHeatmap("##Density",cache.heatmap.data(),cache.heatmapRows,cache.count,-state.heatmapRange,state.heatmapRange,nullptr,
                    {cache.first+.5,cache.timeMax},{cache.first+cache.count+.5,cache.timeMin});
                ImPlot::PopColormap();
            }
            if(state.showWiggle) for(int channel=0;channel<cache.count;++channel) {
                const auto& preview=cache.previews[channel];const double reference=state.normalizeEach?preview.peak:cache.peak;
                WiggleContext2D plot{&preview,double(cache.first+channel+1),.45*state.gain/std::max(reference,1e-30),1};
                ImGui::PushID(channel);
                if(state.positiveFill) {
                    ImPlot::SetNextFillStyle(Seismic::recordPolarityColor(state.palette,true),.32f);
                    ImPlot::PlotShadedG("##Fill",wigglePoint2D,&plot,baselinePoint2D,&plot,int(preview.values.size()));
                    if(state.signedFill) {plot.lobe=-1;ImPlot::SetNextFillStyle(Seismic::recordPolarityColor(state.palette,false),.32f);
                        ImPlot::PlotShadedG("##NegativeFill",wigglePoint2D,&plot,baselinePoint2D,&plot,int(preview.values.size()));}
                }
                plot.lobe=0;ImPlot::SetNextLineStyle(p.text,1.1f);
                ImPlot::PlotLineG("##Wiggle",wigglePoint2D,&plot,int(preview.values.size()));ImGui::PopID();
            }
        }
        ImPlot::EndPlot();state.fitRequest=false;
    }
    if(colorbar) {ImGui::SameLine();ImPlot::ColormapScale(AppUi::text("Normalized amplitude"),-state.heatmapRange,state.heatmapRange,
        ImVec2(colorbarWidth,height),"%.2f",ImPlotColormapScaleFlags_None,palette);}
    ImPlot::PopStyleColor(4);
}
}

// Own viewer UI state independently from solver controls and raw captured/exported data.
void renderRecordViewer2D(ViewerState2D& state) {
    if(!state.isOpen) return;
    AppUi::ThemePanelScope themeColors;
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Acquisition);
    ImGui::SetNextWindowSize(ImVec2(1200,820),ImGuiCond_FirstUseEver);
    if(state.documentationCapture) {
        ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
        const auto* viewport=ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+12,viewport->WorkPos.y+12));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x-24,viewport->WorkSize.y-24));
    }
    if(state.dockingClass) {ImGuiWindowClass windowClass;windowClass.ClassId=state.dockingClass;
        windowClass.DockingAllowUnclassed=false;ImGui::SetNextWindowClass(&windowClass);}
    if(state.focusRequest) {ImGui::SetNextWindowFocus();state.focusRequest=false;}
    const bool visible=AppUi::Widgets::Begin("Seismic Data Analyzer###Seis2D.RecordViewer",&state.isOpen);
    if(visible) {
        try {
            constexpr bool compact=true;
            const auto& record=state.records[state.dualComponent?std::clamp(state.component,0,1):0];
            if(ImGui::BeginTable("RecordToolbar2D",3,ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Source",0,.32f);ImGui::TableSetupColumn("Selection",0,.43f);ImGui::TableSetupColumn("Component",0,.25f);
                ImGui::TableNextColumn();
                if(AppUi::Widgets::Button("Record file / source",ImVec2(-1,0))) ImGui::OpenPopup("RecordSource2D");
                if(ImGui::BeginPopup("RecordSource2D")) {
                    ImGui::SetNextItemWidth(420);AppUi::Widgets::InputText("##RecordPath2D",state.importPath,sizeof(state.importPath));
                    if(AppUi::Widgets::Button("Browse SEG-Y")) browseRecord2D(state);
                    ImGui::SameLine();if(AppUi::Widgets::Button("Import shot for display")) importRecord2D(state,viewerPath2D(state.importPath));
                    AppUi::Widgets::TextWrapped("Imported records and capture snapshots use the same viewer. Original samples are retained.");
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);
                int selection=state.viewMode==1?1:state.showAllTraces?0:2;
                if(AppUi::Widgets::Combo("##RecordSelection2D",&selection,"All traces\0Single trace\0Trace range\0")) {
                    state.viewMode=selection==1?1:0;state.showAllTraces=selection==0;state.fitRequest=true;
                }
                ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);
                if(state.dualComponent) {if(AppUi::Widgets::Combo("##RecordedComponent2D",&state.component,"Vz\0Vx\0")) state.fitRequest=true;}
                else AppUi::Widgets::TextUnformatted(record.name.empty()?AppUi::text("Recorded component"):record.name.c_str());
                ImGui::EndTable();
            }
            if(AppUi::Widgets::Button("Display Parameters")) ImGui::OpenPopup("RecordVisual2D");
            if(ImGui::BeginPopup("RecordVisual2D")) {
                Seismic::drawRecordVisualControls(state.recordStyle,state.palette,state.heatmapRange,state.densityOpacity,state.showWiggle,state.showHeatmap,state.positiveFill,state.signedFill);
                if(AppUi::Widgets::Button("Reset viewer display")) resetViewerDisplay2D(state);
                ImGui::EndPopup();
            }
            ImGui::SameLine();drawTraceOptions2D(state);
            ImGui::SameLine();if(AppUi::Widgets::Button("Reset View")) state.fitRequest=true;
            if(!record.traces.empty()) {
                const int total=int(record.traces.size());
                if(state.viewMode==1) {
                    int channel=std::clamp(state.selectedTrace+1,1,total);ImGui::SetNextItemWidth(-1);
                    if(AppUi::Widgets::SliderInt("Receiver",&channel,1,total)) {state.selectedTrace=channel-1;state.fitRequest=true;}
                } else {
                    int window[2]={state.firstTrace+1,state.showAllTraces?total:state.visibleTraces};
                    ImGui::SetNextItemWidth(std::max(160.0f,ImGui::GetContentRegionAvail().x*.45f));
                    if(AppUi::Widgets::InputInt2("First / visible traces",window)) {
                        state.firstTrace=std::clamp(window[0]-1,0,total-1);state.visibleTraces=std::clamp(window[1],1,total-state.firstTrace);
                        state.showAllTraces=false;state.fitRequest=true;
                    }
                    ImGui::SameLine();if(AppUi::Widgets::SmallButton("Show all traces")) {state.showAllTraces=true;state.firstTrace=0;state.visibleTraces=total;state.fitRequest=true;}
                }
            }
            prepareViewerCache2D(state);
            if(!record.traces.empty()) {
                if(compact) {
                    AppUi::Widgets::TextDisabled("%d traces x %d samples | dt %.3f ms | Raw peak %.2e %s",int(record.traces.size()),int(record.traces.front().size()),record.sampleInterval*1000,state.cache.rawPeak,record.units.c_str());
                    if(state.agc.enabled) {ImGui::SameLine();AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Acquisition),"AGC ON");}
                } else {
                    AppUi::Widgets::TextDisabled("%s | Raw peak %.2e | Display peak %.2e | %s",record.name.c_str(),state.cache.rawPeak,state.cache.peak,record.units.c_str());
                    if(state.agc.enabled) AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Acquisition),"AGC ON | %.0f ms | strength %.2f | max %.0fx",state.agc.windowMs,state.agc.strength,state.agc.maxBoost);
                }
            }
            if(!state.status.empty()&&(!compact||state.error)) AppUi::Widgets::TextWrapped("%s",state.status.c_str());
            Seismic::drawRecordNavigationHelp();
            drawRecordPlot2D(state);
        } catch(const std::exception& error) {state.error=true;state.status=error.what();AppUi::Widgets::TextWrapped("%s",state.status.c_str());}
    }
    ImGui::End();
}
}
