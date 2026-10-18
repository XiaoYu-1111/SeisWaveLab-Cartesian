#include "AppWidgets.h"
#include "HomeScreen.h"
#include "AppInfo.h"
#include "AppTheme.h"
#include "Cuda_Check.cuh"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Blend presentation colors without assigning a scientific meaning to schematic artwork.
ImVec4 blendHome(ImVec4 a,ImVec4 b,float weight) {
    return {a.x+(b.x-a.x)*weight,a.y+(b.y-a.y)*weight,a.z+(b.z-a.z)*weight,1};
}

// Draw scalable card text inside the host clip rectangle.
void cardText(ImDrawList* draw,ImVec2 position,float size,ImVec4 color,const char* text,float wrap=0) {
    draw->AddText(ImGui::GetFont(),size,position,AppUi::color(color),AppUi::text(text),nullptr,wrap);
}

// Explain a 2-D cross section with layer contrast and outward-propagating schematic wavefronts.
void drawPlanarDiagram(ImDrawList* draw,ImVec2 origin,ImVec2 size,const AppUi::ThemePalette& p,float scale) {
    draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),AppUi::color(p.background),6*scale);
    draw->AddRectFilled(ImVec2(origin.x,origin.y+size.y*.56f),ImVec2(origin.x+size.x,origin.y+size.y),
        AppUi::color(blendHome(p.raised,p.accent,.12f)));
    for(int i=1;i<8;++i) draw->AddLine(ImVec2(origin.x+size.x*i/8,origin.y),ImVec2(origin.x+size.x*i/8,origin.y+size.y),AppUi::color(p.border,.3f));
    for(int i=1;i<5;++i) draw->AddLine(ImVec2(origin.x,origin.y+size.y*i/5),ImVec2(origin.x+size.x,origin.y+size.y*i/5),AppUi::color(p.border,.3f));
    const ImVec2 source(origin.x+size.x*.5f,origin.y+size.y*.32f);
    const float maximum=(std::max)(1.0f,size.y*.9f);
    for(int i=0;i<5;++i) {
        const float phase=std::fmod(float(ImGui::GetTime())*.22f+i*.2f,1.0f);
        draw->AddCircle(source,phase*maximum,AppUi::color(p.accent,(1-phase)*.6f),96,1.3f*scale);
    }
    draw->AddCircleFilled(source,3*scale,AppUi::color(p.accent));
    for(int i=0;i<10;++i) draw->AddCircleFilled(ImVec2(origin.x+size.x*(i+1)/11,origin.y+12*scale),2*scale,AppUi::color(p.muted));
    cardText(draw,ImVec2(origin.x+12*scale,origin.y+size.y-23*scale),12*scale,p.muted,"X-Z SECTION / SCHEMATIC");
    draw->PopClipRect();
}

// Explain orthogonal 3-D exploration with a wire volume and independent surface receiver grid.
void drawSpatialDiagram(ImDrawList* draw,ImVec2 origin,ImVec2 size,const AppUi::ThemePalette& p,float scale) {
    draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),AppUi::color(p.background),6*scale);
    const ImVec2 center(origin.x+size.x*.5f,origin.y+size.y*.48f);
    const float radius=(std::min)(size.x*.23f,size.y*.43f);
    const auto project=[&](float x,float y,float z) {return ImVec2(center.x+radius*(x+y*.48f),center.y+radius*(y*.28f-z*.85f));};
    for(int i=0;i<3;++i) for(int a=-1;a<=1;a+=2) for(int b=-1;b<=1;b+=2) {
        std::array<float,3> first{},second{};
        first[i]=-1;second[i]=1;first[(i+1)%3]=second[(i+1)%3]=float(a);first[(i+2)%3]=second[(i+2)%3]=float(b);
        draw->AddLine(project(first[0],first[1],first[2]),project(second[0],second[1],second[2]),AppUi::color(p.accent,.8f),1.3f*scale);
    }
    for(int i=-4;i<=4;++i) {
        const float t=i/5.0f;
        draw->AddLine(project(-1,t,0),project(1,t,0),AppUi::color(p.border,.75f));
        draw->AddLine(project(t,0,-1),project(t,0,1),AppUi::color(p.border,.75f));
    }
    // A surface array communicates depth in this schematic, independently of the deployed survey.
    for(int i=0;i<10;++i) {
        const float t=-.9f+i*.2f;
        draw->AddLine(project(-.9f,t,1),project(.9f,t,1),AppUi::color(p.accent,.16f));
        draw->AddLine(project(t,-.9f,1),project(t,.9f,1),AppUi::color(p.accent,.16f));
    }
    for(int x=0;x<10;++x) for(int y=0;y<10;++y)
        draw->AddCircleFilled(project(-.9f+x*.2f,-.9f+y*.2f,1),1.5f*scale,AppUi::color(p.accent,.65f));
    for(int i=0;i<4;++i) {
        const float phase=std::fmod(float(ImGui::GetTime())*.22f+i*.25f,1.0f);
        std::array<ImVec2,65> points;
        for(int j=0;j<=64;++j) {const float angle=j*6.2831853f/64;points[j]=project(phase*std::cos(angle),phase*std::sin(angle),0);}
        draw->AddPolyline(points.data(),int(points.size()),AppUi::color(p.accent,(1-phase)*.7f),0,1.3f*scale);
    }
    cardText(draw,ImVec2(origin.x+12*scale,origin.y+size.y-23*scale),12*scale,p.muted,"3-SLICE / 10 x 10 RECEIVERS / SCHEMATIC");
    draw->PopClipRect();
}

// Use one clear launch target per card, with consistent keyboard and mouse interaction.
bool drawLaunchCard(bool spatial,ImVec2 size,float scale) {
    auto p=AppUi::palette();
    p.accent=AppUi::functionColor(spatial?AppUi::FunctionRole::Display:AppUi::FunctionRole::Simulation);
    p.border=blendHome(p.border,p.accent,.35f);
    auto* draw=ImGui::GetWindowDrawList();const auto origin=ImGui::GetCursorScreenPos();
    const auto end=ImVec2(origin.x+size.x,origin.y+size.y);
    const bool clicked=AppUi::Widgets::Button(spatial?"##HomeLaunch3D":"##HomeLaunch2D",size);
    const bool active=ImGui::IsItemHovered()||ImGui::IsItemFocused();
    draw->AddRectFilled(origin,end,AppUi::color(active?blendHome(p.panel,p.accent,.045f):p.panel),9*scale);
    draw->AddRect(origin,end,AppUi::color(active?p.accent:p.border),9*scale,0,active?1.6f:1.0f);
    draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+3*scale),AppUi::color(p.accent),3*scale);
    const float pad=24*scale;
    cardText(draw,ImVec2(origin.x+pad,origin.y+20*scale),12*scale,p.accent,spatial?"3D SPATIAL EXPLORATION":"2D WAVEFIELD SANDBOX");
    cardText(draw,ImVec2(origin.x+pad,origin.y+44*scale),25*scale,p.text,spatial?"三维空间探索沙盒":"二维弹性波模拟沙盒");
    cardText(draw,ImVec2(origin.x+pad,origin.y+85*scale),15*scale,p.muted,
        spatial?"正交切片、体绘制与三分量数据采集":"介质建模、震源配置与实时波场分析",size.x-2*pad);
    const auto imageOrigin=ImVec2(origin.x+pad,origin.y+121*scale);
    const auto imageSize=ImVec2(size.x-2*pad,(std::max)(70.0f,size.y-209*scale));
    if(spatial) drawSpatialDiagram(draw,imageOrigin,imageSize,p,scale);else drawPlanarDiagram(draw,imageOrigin,imageSize,p,scale);
    cardText(draw,ImVec2(origin.x+pad,origin.y+size.y-73*scale),13*scale,p.muted,
        spatial?"独立工作区  /  CPML  /  最多 300 道":"交错网格  /  模型编辑  /  SEG-Y");
    draw->AddLine(ImVec2(origin.x+pad,origin.y+size.y-48*scale),ImVec2(end.x-pad,origin.y+size.y-48*scale),AppUi::color(p.border,.6f));
    cardText(draw,ImVec2(origin.x+pad,origin.y+size.y-33*scale),16*scale,p.accent,spatial?"进入三维工作区  →":"进入二维工作区  →");
    return clicked;
}

// Give the lower workspace useful scientific workflow guidance, reflowing cards on narrow windows.
void drawWorkflowCards(float scale) {
    const float width=ImGui::GetContentRegionAvail().x;
    const int columns=width>=800*scale?4:width>=440*scale?2:1;
    const char* titles[]={"建立介质模型","配置震源与边界","计算与数据采集","分析与导出"};
    const char* details[]={"定义 Vp、Vs、密度与网格，检查模型分层。","设置震源位置、主频、CPML 与自由表面。",
        "监测正交剖面与三分量接收数据。","对比波场样式，导出模型与 SEG-Y 道集。"};
    const char* labels[]={"MODEL / Vp · Vs · rho","SOURCE / CFL · CPML","ACQUIRE / Vx · Vy · Vz","EXPORT / MODEL · SEG-Y"};
    const AppUi::FunctionRole roles[]={AppUi::FunctionRole::Model,AppUi::FunctionRole::Source,
        AppUi::FunctionRole::Acquisition,AppUi::FunctionRole::Display};
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(5*scale,3*scale));
    if(ImGui::BeginTable("Scientific workflow",columns,ImGuiTableFlags_SizingStretchSame)) {
        for(int i=0;i<4;++i) {
            ImGui::TableNextColumn();ImGui::PushID(i);
            AppUi::FunctionStyleScope colors(roles[i]);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14*scale,11*scale));
            ImGui::BeginChild("Workflow step",ImVec2(0,112*scale),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            const auto accent=AppUi::functionColor(roles[i]);
            const auto position=ImGui::GetWindowPos(),extent=ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(position.x+7,position.y+2),ImVec2(position.x+extent.x-7,position.y+2),AppUi::color(accent,.75f),2);
            ImGui::PushFont(nullptr,16*scale);AppUi::Widgets::TextColored(accent,"%02d",i+1);ImGui::SameLine();
            AppUi::Widgets::TextUnformatted(titles[i]);ImGui::PopFont();
            ImGui::PushFont(nullptr,13*scale);AppUi::Widgets::TextWrapped("%s",details[i]);
            ImGui::SetCursorPosY(88*scale);AppUi::Widgets::TextDisabled("%s",labels[i]);ImGui::PopFont();
            ImGui::EndChild();ImGui::PopStyleVar();ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

// Show honest startup guidance and discoverable navigation gestures without allocating a model.
void drawWorkspaceGuide(float scale) {
    const bool wide=ImGui::GetContentRegionAvail().x>=800*scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14*scale,9*scale));
    ImGui::BeginChild("Workspace guide",ImVec2(0,(wide?64.0f:112.0f)*scale),ImGuiChildFlags_Borders);
    if(ImGui::BeginTable("Workspace guide columns",wide?2:1,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();ImGui::PushFont(nullptr,14*scale);
        AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Model),"3D 起步建议");ImGui::SameLine();AppUi::Widgets::TextDisabled("300 × 50 × 300 / 200³ / 300³");
        ImGui::PushFont(nullptr,13*scale);AppUi::Widgets::TextDisabled("先使用小网格确认模型与震源；初始化前检查显存预算。");ImGui::PopFont();
        ImGui::TableNextColumn();AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Display),"剖面操作");ImGui::SameLine();AppUi::Widgets::TextDisabled("滚轮缩放 · 中键平移 · 左键联动");
        ImGui::PushFont(nullptr,13*scale);AppUi::Widgets::TextDisabled("Reset view 恢复单剖面；Reset views 恢复全部视图。");ImGui::PopFont();
        ImGui::PopFont();ImGui::EndTable();
    }
    ImGui::EndChild();ImGui::PopStyleVar();
}

// Draw a compact animated data-flow strip with deterministic geometry and bounded drawing cost.
void drawDataFlow(ImVec2 origin,float width,float scale) {
    const auto& p=AppUi::palette();auto* draw=ImGui::GetWindowDrawList();
    for(int channel=0;channel<3;++channel) {
        std::array<ImVec2,97> points;
        for(int i=0;i<=96;++i) {
            const float u=i/96.0f;
            const float wave=std::sin(u*30+float(ImGui::GetTime())*.6f+channel)*std::sin(u*3.14159f);
            points[i]=ImVec2(origin.x+u*width,origin.y+channel*5*scale+wave*7*scale);
        }
        draw->AddPolyline(points.data(),int(points.size()),AppUi::color(p.accent,.15f+channel*.07f),0,1);
    }
}
} // namespace

// Compose a responsive scientific launch workspace with factual device status and persistent themes.
AppScreen drawHomeScreen(const GpuInfo& info,int width,int height) {
    (void)width;(void)height;
    const auto* viewport=ImGui::GetMainViewport();
    const float scale=(std::clamp)(viewport->WorkSize.x/1280.0f,.85f,1.25f);
    const float margin=32*scale;
    const auto& p=AppUi::palette();
    ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(margin,24*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushFont(nullptr,18*scale);
    AppUi::Widgets::Begin("Scientific launch workspace",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    AppScreen route=AppScreen::Intro0;
    if(ImGui::BeginTable("HomeHeader",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Title",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Theme",ImGuiTableColumnFlags_WidthFixed,215*scale);
        ImGui::TableNextColumn();ImGui::PushFont(nullptr,(AppUi::currentLanguage()==AppUi::Language::English?25:30)*scale);
        AppUi::Widgets::TextColored(p.text,"%s",AppUi::productName);ImGui::PopFont();
        AppUi::Widgets::TextDisabled("Cartesian Elastic Wave Simulation | v%s",AppUi::productVersion);
        ImGui::TableNextColumn();AppUi::Widgets::TextDisabled("Theme / Language");AppUi::drawThemeSelector("##HomeTheme",-1);
        AppUi::drawLanguageSelector(-1);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0,16*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16*scale,12*scale));
    ImGui::BeginChild("Device summary",ImVec2(0,104*scale),ImGuiChildFlags_Borders);
    AppUi::Widgets::TextColored(AppUi::functionColor(info.success?AppUi::FunctionRole::Simulation:AppUi::FunctionRole::Danger),info.success?"CUDA DEVICE READY":"CUDA DEVICE UNAVAILABLE");
    if(ImGui::BeginTable("DeviceSummaryColumns",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Device",ImGuiTableColumnFlags_WidthStretch);
        const float manualWidth=std::max(160*scale,ImGui::CalcTextSize(AppUi::label("Operating manual")).x+2*ImGui::GetStyle().FramePadding.x+12*scale);
        ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthFixed,manualWidth);
        ImGui::TableNextColumn();AppUi::Widgets::TextUnformatted(info.success?info.name:"未检测到可用 CUDA 设备");
        if(info.success) AppUi::Widgets::TextDisabled("%.1f GiB VRAM / SM %d.%d / %d multiprocessors",
            double(info.totalMem)/(1024.0*1024.0*1024.0),info.computeCapabilityMajor,info.computeCapabilityMinor,info.multiProcessorCount);
        ImGui::TableNextColumn();if(AppUi::Widgets::Button("Operating manual",ImVec2(-1,0))) route=AppScreen::UserManual;
        ImGui::EndTable();
    }
    ImGui::EndChild();ImGui::PopStyleVar();ImGui::Dummy(ImVec2(0,14*scale));
    const bool sideBySide=ImGui::GetContentRegionAvail().x>=800*scale;
    // Reserve space for the complete workflow and guide, then use the remaining height for diagrams.
    // There is no upper height cap: a tall window no longer leaves an unused area below the footer.
    const auto& spacing=ImGui::GetStyle();
    const float lowerHeight=271*scale+2*spacing.CellPadding.y+2*spacing.SeparatorTextPadding.y+1+13*spacing.ItemSpacing.y;
    const float cardHeight=sideBySide?(std::max)(290*scale,ImGui::GetContentRegionAvail().y-lowerHeight):340*scale;
    if(ImGui::BeginTable("Sandbox launch cards",sideBySide?2:1,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();if(drawLaunchCard(false,ImVec2(ImGui::GetContentRegionAvail().x,cardHeight),scale)) route=AppScreen::SeisSim_GPU;
        ImGui::TableNextColumn();if(drawLaunchCard(true,ImVec2(ImGui::GetContentRegionAvail().x,cardHeight),scale)) route=AppScreen::SeisSim_3D;
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0,12*scale));AppUi::Widgets::SeparatorText("WORKFLOW / 计算工作流");
    drawWorkflowCards(scale);ImGui::Dummy(ImVec2(0,8*scale));drawWorkspaceGuide(scale);
    // Keep the data-flow/footer at the bottom when a large work area has spare vertical space.
    const float footerHeight=43*scale+1+4*spacing.ItemSpacing.y;
    ImGui::Dummy(ImVec2(0,(std::max)(8*scale,ImGui::GetContentRegionAvail().y-footerHeight)));
    const auto flowOrigin=ImGui::GetCursorScreenPos();drawDataFlow(flowOrigin,ImGui::GetContentRegionAvail().x,scale);
    ImGui::Dummy(ImVec2(0,25*scale));ImGui::Separator();
    AppUi::Widgets::TextDisabled("%s  /  v%s  /  XIAO YU  /  MIT LICENSE",AppUi::productName,AppUi::productVersion);
    ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(3);
    return route;
}
