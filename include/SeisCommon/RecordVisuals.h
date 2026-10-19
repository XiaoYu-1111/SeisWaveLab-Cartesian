#pragma once
#include "WavePalettes.h"
#include "AppWidgets.h"
#include "implot.h"
#include <string>
#include <vector>
#include <cmath>

namespace Seismic {
// Reserve tick labels and the vertical title at the actual UI font size, including Chinese text.
inline float recordColorbarWidth() {
    return (std::max)(100.0f,ImGui::CalcTextSize("-1.00").x+ImGui::GetFontSize()*3);
}
struct SingleTracePlot {const std::vector<double>* times;const std::vector<double>* values;int lobe;};
// Supply a true time/amplitude point, optionally restricted to one polarity for variable-area shading.
inline ImPlotPoint singleTracePoint(int index,void* pointer) {
    const auto& plot=*static_cast<SingleTracePlot*>(pointer);const double value=(*plot.values)[index];
    return {(*plot.times)[index],plot.lobe>0?std::max(0.0,value):plot.lobe<0?std::min(0.0,value):value};
}
// Keep polarity fills aligned to zero amplitude at the actual sample timestamps.
inline ImPlotPoint singleTraceBaseline(int index,void* pointer) {
    const auto& plot=*static_cast<SingleTracePlot*>(pointer);return {(*plot.times)[index],0};
}
inline constexpr const char* recordStyleNames="Wiggle\0Variable area\0Density image\0Density + wiggle\0Signed lobes\0Density + signed lobes\0Custom\0";
// Apply presentation flags only; palette, gain, AGC and original amplitudes retain their values.
inline void applyRecordStyle(int style,bool& wiggle,bool& density,bool& positive,bool& signedFill) {
    if(style<0||style>5) return;
    wiggle=style!=2;density=style==2||style==3||style==5;
    positive=style==1||style==3||style==4||style==5;signedFill=style==4||style==5;
}
// Register per ImPlot context; never retain a colormap ID across destruction of that context.
inline ImPlotColormap recordColormap(int palette,float opacity=1) {
    palette=std::clamp(palette,0,13);
    const int alpha=std::clamp(int(std::lround(std::isfinite(opacity)?opacity*100:100)),5,100);
    const auto name="SeisWaveRecordPalette"+std::to_string(palette)+"Alpha"+std::to_string(alpha);
    const auto found=ImPlot::GetColormapIndex(name.c_str());if(found>=0) return found;
    ImVec4 stops[5];const auto& values=wavePalette(palette);
    for(int i=0;i<5;++i) stops[i]=ImVec4(values[i][0],values[i][1],values[i][2],alpha*.01f);
    return ImPlot::AddColormap(name.c_str(),stops,5,false);
}
// Obtain polarity colors from the same definitions as the density palette and wavefield.
inline ImVec4 recordPolarityColor(int palette,bool positive) {
    const auto& color=wavePalette(palette)[positive?4:0];return ImVec4(color[0],color[1],color[2],1);
}
// Keep the 2D and 3D appearance controls identical while each viewer owns its independent settings.
inline void drawRecordVisualControls(int& style,int& palette,float& range,float& opacity,bool& wiggle,
    bool& density,bool& positive,bool& signedFill) {
    ImGui::SetNextItemWidth(260);
    if(AppUi::Widgets::Combo("Record style",&style,recordStyleNames)) applyRecordStyle(style,wiggle,density,positive,signedFill);
    ImGui::SetNextItemWidth(260);AppUi::Widgets::Combo("Wave palette",&palette,wavePaletteNames);
    ImGui::SetNextItemWidth(260);AppUi::Widgets::SliderFloat("Heatmap range",&range,.01f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(260);AppUi::Widgets::SliderFloat("Density opacity",&opacity,.05f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    bool custom=AppUi::Widgets::Checkbox("Show Wiggle",&wiggle);ImGui::SameLine();custom|=AppUi::Widgets::Checkbox("Show Heatmap",&density);
    custom|=AppUi::Widgets::Checkbox("Signed polarity fill",&signedFill);
    if(custom) {style=6;if(signedFill) positive=true;}
    AppUi::Widgets::TextWrapped("Palette and density range affect display only. Gain, AGC and raw samples are retained.");
}
// Release automatic fitting before ImPlot processes a wheel/drag gesture over this plot or its axes.
inline void beginRecordNavigation(bool& autoFit,bool& fit,ImVec2 size) {
    const auto origin=ImGui::GetCursorScreenPos(),mouse=ImGui::GetIO().MousePos;
    const bool inside=mouse.x>=origin.x&&mouse.y>=origin.y&&mouse.x<origin.x+size.x&&mouse.y<origin.y+size.y;
    const bool gesture=ImGui::GetIO().MouseWheel!=0||ImGui::IsMouseDragging(ImGuiMouseButton_Left)
        ||ImGui::IsMouseDragging(ImGuiMouseButton_Right)||ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
    if(inside&&gesture&&ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {autoFit=false;fit=false;}
}
// Explain native independent-axis navigation in a compact popup shared by both viewers.
inline void drawRecordNavigationHelp() {
    if(AppUi::Widgets::SmallButton("Navigation")) ImGui::OpenPopup("RecordNavigation");
    if(ImGui::BeginPopup("RecordNavigation")) {
        AppUi::Widgets::TextWrapped("Wheel over plot: zoom both axes. Wheel over an axis: zoom that axis only. Left drag: pan. Reset View: fit the data. Manual navigation disables Auto fit.");
        ImGui::EndPopup();
    }
}
}
