#include "AppWidgets.h"
#include "HomeScreen.h"
#include "AppInfo.h"
#include "AppTheme.h"
#include "Cuda_Check.cuh"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
void text(ImDrawList* d,ImVec2 at,float size,ImVec4 ink,const char* label,float wrap=0) {
    d->AddText(ImGui::GetFont(),size,at,AppUi::color(ink),AppUi::text(label),nullptr,wrap);
}
// Affine Cartesian geometry: equal spacing, orthogonal sections, no simulated field data.
void diagram(ImDrawList* d,ImVec2 at,ImVec2 size,bool spatial,float scale) {
    const auto& p=AppUi::palette();
    const auto point=[&](float x,float y){return ImVec2(at.x+x*size.x,at.y+y*size.y);};
    const auto ink=[&](float alpha){return AppUi::color(p.text,alpha);};
    d->PushClipRect(at,point(1,1),true);
    d->AddRectFilledMultiColor(at,point(1,1),AppUi::color(p.panel),AppUi::color(p.panel),
        AppUi::color(p.background),AppUi::color(p.background));
    if(!spatial) {
        const float x0=.07f,x1=.93f,y0=.14f,y1=.90f;
        const float step=(std::max)(5.0f,6*scale);
        const int nx=(std::max)(8,int(size.x*(x1-x0)/step));
        const int nz=(std::max)(8,int(size.y*(y1-y0)/step));
        for(int i=0;i<=nx;++i) {
            const float x=x0+(x1-x0)*i/nx;
            d->AddLine(point(x,y0),point(x,y1),ink(i%8?.14f:.30f));
        }
        for(int j=0;j<=nz;++j) {
            const float z=y0+(y1-y0)*j/nz;
            d->AddLine(point(x0,z),point(x1,z),ink(j%8?.14f:.30f));
        }
        d->AddRect(point(x0,y0),point(x1,y1),ink(.5f));
        d->AddLine(point(x0,y0),point(x1,y0),ink(.8f),1.4f*scale);
        const auto source=point(.5f,.40f);
        d->AddCircle(source,4*scale,ink(.8f),24,1.2f);
        d->AddCircleFilled(source,1.3f*scale,ink(.8f));
        text(d,point(.075f,.035f),16*scale,p.muted,"X-Z  /  P-SV  /  ORTHOGONAL GRID");
        text(d,point(.94f,.91f),16*scale,p.muted,"X");
        text(d,point(.025f,.88f),16*scale,p.muted,"Z");
    } else {
        const auto project=[&](float x,float y,float z){return point(.08f+.59f*x+.25f*y,.13f+.17f*y+.62f*z);};
        const int n=(std::clamp)(int(size.x/(7*scale)),40,150);
        // Three intersecting coordinate planes reveal the tensor-product volume.
        for(int plane=0;plane<3;++plane) {
            const auto mesh=[&](float u,float v){return plane==0?project(u,.5f,v):plane==1?project(.5f,u,v):project(u,v,0);};
            for(int i=0;i<=n;++i) {
                const float t=float(i)/n;
                const float alpha=plane==2?(i%8?.22f:.47f):(i%8?.10f:.25f);
                d->AddLine(mesh(t,0),mesh(t,1),ink(alpha));
                d->AddLine(mesh(0,t),mesh(1,t),ink(alpha));
            }
        }
        for(int axis=0;axis<3;++axis) for(int a=0;a<=1;++a) for(int b=0;b<=1;++b) {
            float v[3]={},w[3]={};v[axis]=0;w[axis]=1;
            v[(axis+1)%3]=w[(axis+1)%3]=float(a);v[(axis+2)%3]=w[(axis+2)%3]=float(b);
            d->AddLine(project(v[0],v[1],v[2]),project(w[0],w[1],w[2]),ink(.52f),1.1f*scale);
        }
        const auto source=project(.5f,.5f,.35f);
        d->AddCircle(source,4*scale,ink(.8f),24,1.2f);
        d->AddCircleFilled(source,1.3f*scale,ink(.8f));
        text(d,point(.055f,.035f),16*scale,p.muted,"X-Y-Z  /  CARTESIAN VOLUME");
        text(d,point(.66f,.77f),16*scale,p.muted,"X");
        text(d,point(.93f,.92f),16*scale,p.muted,"Y");
        text(d,point(.035f,.75f),16*scale,p.muted,"Z");
    }
    d->PopClipRect();
}

bool launchCard(bool spatial,ImVec2 size,float scale) {
    const auto& p=AppUi::palette();const auto at=ImGui::GetCursorScreenPos();
    auto* d=ImGui::GetWindowDrawList();
    const auto accent=p.light?(spatial?ImVec4(.23f,.49f,.29f,1):ImVec4(.25f,.40f,.67f,1)):
        (spatial?ImVec4(.49f,.75f,.53f,1):ImVec4(.47f,.65f,.88f,1));
    const bool clicked=AppUi::Widgets::Button(spatial?"##HomeLaunch3D":"##HomeLaunch2D",size);
    const bool hovered=ImGui::IsItemHovered()||ImGui::IsItemFocused();
    const ImVec2 end(at.x+size.x,at.y+size.y);const float pad=16*scale;
    d->AddRectFilled(at,end,AppUi::color(p.panel),3*scale);
    d->AddRect(at,end,AppUi::color(hovered?accent:p.border),3*scale,0,hovered?1.5f:1);
    d->AddLine(at,ImVec2(end.x,at.y),AppUi::color(accent),2*scale);
    text(d,ImVec2(at.x+pad,at.y+15*scale),25*scale,accent,spatial?"3D  直角体网格":"2D  正交剖面");
    text(d,ImVec2(at.x+pad,at.y+53*scale),17*scale,p.text,spatial?"X-Y-Z 空间 · 弹性波 · 九场求解":"X-Z 平面应变 · P-SV · 五场求解");
    const float bottom=170*scale;
    const ImVec2 imageAt(at.x+pad,at.y+90*scale),imageSize(size.x-2*pad,(std::max)(80*scale,size.y-90*scale-bottom));
    diagram(d,ImVec2(imageAt.x+imageSize.x*.04f,imageAt.y+imageSize.y*.03f),
        ImVec2(imageSize.x*.92f,imageSize.y*.94f),spatial,scale);
    const float y=imageAt.y+imageSize.y+9*scale;
    text(d,ImVec2(at.x+pad,y),15*scale,p.muted,"程序绘制的直角网格示意");
    text(d,ImVec2(at.x+pad,y+30*scale),16*scale,p.text,spatial?"正交切片与体视图，接收矩阵与三分量记录。":"分层介质与模型编辑，震源测线与双分量记录。",size.x-2*pad);
    text(d,ImVec2(at.x+pad,y+64*scale),16*scale,p.muted,spatial?"研究空间传播、波型转换与观测系统":"用于剖面传播机理与参数试验",size.x-2*pad);
    const ImVec2 buttonAt(at.x+pad,end.y-60*scale),buttonEnd(end.x-pad,end.y-16*scale);
    d->AddRectFilled(buttonAt,buttonEnd,AppUi::color(accent,hovered?.25f:.16f),2*scale);
    d->AddRect(buttonAt,buttonEnd,AppUi::color(accent,.5f),2*scale);
    const char* label=AppUi::text(spatial?"进入三维直角模拟":"进入二维直角模拟");
    const float font=17*scale;const auto extent=ImGui::GetFont()->CalcTextSizeA(font,10000,0,label);
    d->AddText(ImGui::GetFont(),font,ImVec2((buttonAt.x+buttonEnd.x-extent.x)*.5f,(buttonAt.y+buttonEnd.y-extent.y)*.5f),AppUi::color(p.text),label);
    return clicked;
}
void waveform(ImVec2 at,float width,float scale) {
    auto* d=ImGui::GetWindowDrawList();const auto& p=AppUi::palette();
    const double time=ImGui::GetTime();
    for(int j=0;j<3;++j) {
        std::array<ImVec2,481> points;
        for(int i=0;i<=480;++i) {
            const double u=double(i)/480;
            const double envelope=std::sin(3.141592653589793*u);
            points[i]=ImVec2(at.x+float(u)*width,at.y+12*scale+float((.72*std::sin(u*24-time*1.25+j*.65)+.28*std::sin(u*39+time*.72+j*.9))*envelope)*9*scale);
        }
        d->AddPolyline(points.data(),int(points.size()),AppUi::color(j==0?p.accent:p.muted,j==0?.65f:.25f),0,1);
    }
}
}

AppScreen drawHomeScreen(const GpuInfo& info,int width,int height) {
    (void)width;(void)height;
    const auto* v=ImGui::GetMainViewport();const auto& p=AppUi::palette();
    const float scale=(std::clamp)(v->WorkSize.x/1600.0f,.85f,1.25f),pad=22*scale;
    ImGui::SetNextWindowPos(v->WorkPos);ImGui::SetNextWindowSize(v->WorkSize);ImGui::SetNextWindowViewport(v->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(pad,18*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushFont(nullptr,18*scale);
    AppUi::Widgets::Begin("Scientific launch workspace",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    AppScreen route=AppScreen::Intro0;
    if(ImGui::BeginTable("HomeHeader",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Brand",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Settings",ImGuiTableColumnFlags_WidthFixed,485*scale);
        ImGui::TableNextColumn();AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Source),"SEISWAVE LAB");
        ImGui::SameLine();AppUi::Widgets::TextDisabled("CARTESIAN");
        ImGui::TableNextColumn();AppUi::drawLanguageSelector(135*scale);ImGui::SameLine();
        AppUi::drawThemeSelector("##HomeTheme",160*scale);ImGui::SameLine();
        if(AppUi::Widgets::Button("Operating manual")) route=AppScreen::UserManual;
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0,12*scale));ImGui::PushFont(nullptr,30*scale);
    AppUi::Widgets::TextUnformatted("直角坐标地震波场模拟");ImGui::PopFont();
    AppUi::Widgets::TextDisabled("正交交错网格 · 弹性波传播 · 二维剖面与三维空间");
    ImGui::Dummy(ImVec2(0,9*scale));
    if(ImGui::BeginTable("Home workflow",5,ImGuiTableFlags_SizingStretchSame)) {
        const char* steps[]={"01  介质建模","02  震源与接收","03  并行求解","04  波场与记录","05  数据导出"};
        const AppUi::FunctionRole roles[]={AppUi::FunctionRole::Model,AppUi::FunctionRole::Source,AppUi::FunctionRole::Simulation,AppUi::FunctionRole::Display,AppUi::FunctionRole::Acquisition};
        for(int i=0;i<5;++i) {ImGui::TableNextColumn();AppUi::Widgets::TextColored(AppUi::functionColor(roles[i]),"%s",AppUi::text(steps[i]));}
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0,7*scale));
    const bool wide=ImGui::GetContentRegionAvail().x>=700*scale;
    const float cardHeight=wide?(std::max)(380*scale,ImGui::GetContentRegionAvail().y-110*scale):510*scale;
    if(ImGui::BeginTable("Sandbox launch cards",wide?2:1,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();if(launchCard(false,ImVec2(ImGui::GetContentRegionAvail().x,cardHeight),scale)) route=AppScreen::SeisSim_GPU;
        ImGui::TableNextColumn();if(launchCard(true,ImVec2(ImGui::GetContentRegionAvail().x,cardHeight),scale)) route=AppScreen::SeisSim_3D;
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0,8*scale));
    AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Simulation),
        info.success?"CPU / OpenMP + CUDA READY":"CPU / OpenMP READY");
    ImGui::SameLine();AppUi::Widgets::TextDisabled("| %s",info.success?info.name:"CUDA unavailable; CPU fallback enabled");
    AppUi::Widgets::TextDisabled("XIAO YU  |  C++ / OpenMP / CUDA / OpenGL / GLFW / Dear ImGui / ImPlot / CMake");
    waveform(ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail().x,scale);ImGui::Dummy(ImVec2(0,27*scale));
    ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(3);
    return route;
}
