#include "AppWidgets.h"
#include "Internal3D.h"
#include "AppTheme.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Seis3D::Detail3D {
namespace {
constexpr float maximumSliceZoom3D=32.0f;

struct SliceBounds3D {
    ImVec2 minimum3D,maximum3D;
};

// Constrain a normalized camera to the actual texture, including after zooming back out.
void constrainSliceCamera3D(SliceCamera3D& camera) {
    if(!std::isfinite(camera.zoom3D)) camera=SliceCamera3D{};
    camera.zoom3D=(std::clamp)(camera.zoom3D,1.0f,maximumSliceZoom3D);
    const float halfExtent=0.5f/camera.zoom3D;
    for(float& center:camera.center3D) {
        if(!std::isfinite(center)) center=0.5f;
        center=(std::clamp)(center,halfExtent,1.0f-halfExtent);
    }
}

// Use identical UV bounds for drawing, overlays and coordinate picking on all three planes.
SliceBounds3D sliceBounds3D(const Seis3D_State& s,int plane,SliceCamera3D& camera,ImVec2 visibleExtent) {
    const ImVec2 halfExtent(visibleExtent.x*.5f,visibleExtent.y*.5f);
    camera.center3D[0]=(std::clamp)(camera.center3D[0],halfExtent.x,1-halfExtent.x);
    camera.center3D[1]=(std::clamp)(camera.center3D[1],halfExtent.y,1-halfExtent.y);
    const auto& l=s.gl3D.layout3D;const auto& c=s.context3D;
    const int axes[2]={plane==1?1:0,plane==2?1:2};
    const int physical[3]={c.nx3D,c.ny3D,c.nz3D};
    float start[2]{},extent[2]{1,1};
    if(!s.showBoundary3D) for(int j=0;j<2;++j) {
        const int a=axes[j];start[j]=-float(l.domainOrigin3D[a])/l.domainSize3D[a];
        extent[j]=float(physical[a])/l.domainSize3D[a];
    }
    return {{start[0]+(camera.center3D[0]-halfExtent.x)*extent[0],start[1]+(camera.center3D[1]-halfExtent.y)*extent[1]},
        {start[0]+(camera.center3D[0]+halfExtent.x)*extent[0],start[1]+(camera.center3D[1]+halfExtent.y)*extent[1]}};
}

// Scale the entire fitted section about its current center, independent of mouse position.
void zoomSlice3D(SliceCamera3D& camera,float wheel) {
    camera.zoom3D=(std::clamp)(camera.zoom3D*std::exp((std::clamp)(wheel,-20.0f,20.0f)*0.18f),1.0f,maximumSliceZoom3D);
    constrainSliceCamera3D(camera);
}

// Transform physical cell-center positions through the same camera used to crop the slice texture.
ImVec2 projectSlicePoint3D(float x,float y,int width,int height,const SliceBounds3D& bounds,ImVec2 origin,ImVec2 size) {
    return {origin.x+((x+0.5f)/float(width)-bounds.minimum3D.x)*size.x/(bounds.maximum3D.x-bounds.minimum3D.x),
        origin.y+((y+0.5f)/float(height)-bounds.minimum3D.y)*size.y/(bounds.maximum3D.y-bounds.minimum3D.y)};
}

// Clip physical grids, linked crosshairs and source/receiver markers to the magnified view.
void drawSliceOverlay3D(const Seis3D_State& s,int plane,const SliceBounds3D& bounds,ImVec2 origin,ImVec2 size) {
    const auto& c=s.context3D;
    const auto& layout=s.gl3D.layout3D;
    const int width=layout.sliceWidth3D[plane],height=layout.sliceHeight3D[plane];
    const int physicalWidth=plane==1?c.ny3D:c.nx3D,physicalHeight=plane==2?c.ny3D:c.nz3D;
    const int offsetX=layout.domainOrigin3D[plane==1?1:0],offsetY=layout.domainOrigin3D[plane==2?1:2];
    const int a=plane==1?s.slice3D[1]:s.slice3D[0],b=plane==2?s.slice3D[1]:s.slice3D[2];
    const bool lightCanvas=s.modelStyle3D==4;
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x+size.x,origin.y+size.y);
    draw->PushClipRect(origin,end,true);
    const ImVec2 crosshair=projectSlicePoint3D(float(a-offsetX),float(b-offsetY),width,height,bounds,origin,size);
    if(s.showBoundary3D) {
        // Tint only real external CPML samples; the outlined rectangle is the physical model.
        const ImVec2 first=projectSlicePoint3D(-float(offsetX)-.5f,-float(offsetY)-.5f,width,height,bounds,origin,size);
        const ImVec2 last=projectSlicePoint3D(float(physicalWidth-offsetX)-.5f,float(physicalHeight-offsetY)-.5f,width,height,bounds,origin,size);
        const ImVec2 clippedFirst((std::clamp)(first.x,origin.x,end.x),(std::clamp)(first.y,origin.y,end.y));
        const ImVec2 clippedLast((std::clamp)(last.x,origin.x,end.x),(std::clamp)(last.y,origin.y,end.y));
        const ImU32 tint=ImGui::ColorConvertFloat4ToU32(ImVec4(.9f,.56f,.12f,s.boundaryOpacity3D));
        if(offsetX<0) draw->AddRectFilled(origin,ImVec2(clippedFirst.x,end.y),tint);
        if(offsetY<0) draw->AddRectFilled(ImVec2(clippedFirst.x,origin.y),ImVec2(clippedLast.x,clippedFirst.y),tint);
        if(clippedLast.x<end.x) draw->AddRectFilled(ImVec2(clippedLast.x,origin.y),end,tint);
        if(clippedLast.y<end.y) draw->AddRectFilled(ImVec2(clippedFirst.x,clippedLast.y),ImVec2(clippedLast.x,end.y),tint);
        draw->AddRect(first,last,AppUi::color(AppUi::palette().accent,.95f),0,0,1.5f);
    }
    if(s.showCrosshair3D) {
        const auto crosshairColor=lightCanvas?IM_COL32(35,108,111,150):IM_COL32(149,221,146,150);
        draw->AddLine(ImVec2(crosshair.x,origin.y),ImVec2(crosshair.x,end.y),crosshairColor);
        draw->AddLine(ImVec2(origin.x,crosshair.y),ImVec2(end.x,crosshair.y),crosshairColor);
    }
    if(s.showGrid3D) {
        const ImU32 gridColor=ImGui::ColorConvertFloat4ToU32(lightCanvas?ImVec4(.25f,.36f,.38f,s.gridOpacity3D):ImVec4(.7f,.83f,.82f,s.gridOpacity3D));
        for(int i=1;i<5;++i) {
            const float fraction=float(i)/5;
            const float u=origin.x+((fraction*physicalWidth-offsetX)/width-bounds.minimum3D.x)*size.x/(bounds.maximum3D.x-bounds.minimum3D.x);
            const float v=origin.y+((fraction*physicalHeight-offsetY)/height-bounds.minimum3D.y)*size.y/(bounds.maximum3D.y-bounds.minimum3D.y);
            draw->AddLine(ImVec2(u,origin.y),ImVec2(u,end.y),gridColor);
            draw->AddLine(ImVec2(origin.x,v),ImVec2(end.x,v),gridColor);
        }
    }
    // Intersect the fixed plane before projecting, so unrelated 3-D points never appear on a slice.
    const auto project=[&](int px,int py,int pz,ImU32 color,float radius) {
        const bool intersects=plane==0?std::abs(py-s.slice3D[1])<=1:plane==1?std::abs(px-s.slice3D[0])<=1:std::abs(pz-s.slice3D[2])<=1;
        if(!intersects) return;
        const int u=plane==1?py:px,v=plane==2?py:pz;
        draw->AddCircleFilled(projectSlicePoint3D(float(u-offsetX),float(v-offsetY),width,height,bounds,origin,size),radius,color);
    };
    project(s.source3D.x3D,s.source3D.y3D,s.source3D.z3D,lightCanvas?IM_COL32(179,79,18,255):IM_COL32(255,188,69,255),4);
    for(const auto& receiver:s.receivers3D) project(receiver.x3D,receiver.y3D,receiver.z3D,lightCanvas?IM_COL32(20,111,143,230):IM_COL32(90,218,247,220),2);
    draw->PopClipRect();
    draw->AddRect(origin,end,AppUi::color(AppUi::palette().accent,.7f));
    // Compact physical rulers stay outside the image, keeping geological pixels unobscured.
    const float fontSize=(std::max)(12.0f,ImGui::GetFontSize()*.76f);
    const auto labelColor=AppUi::color(AppUi::palette().muted);
    for(int tick=0;tick<3;++tick) {
        const float fraction=float(tick)*.5f;
        const float u=bounds.minimum3D.x+fraction*(bounds.maximum3D.x-bounds.minimum3D.x);
        const float v=bounds.minimum3D.y+fraction*(bounds.maximum3D.y-bounds.minimum3D.y);
        const float x=(std::clamp)(u*float(width)-.5f+offsetX,s.showBoundary3D?float(offsetX):0.0f,
            float(s.showBoundary3D?width+offsetX-1:physicalWidth-1))*c.spacing3D;
        const float y=(std::clamp)(v*float(height)-.5f+offsetY,s.showBoundary3D?float(offsetY):0.0f,
            float(s.showBoundary3D?height+offsetY-1:physicalHeight-1))*c.spacing3D;
        char horizontal[32]{},vertical[32]{};
        std::snprintf(horizontal,sizeof(horizontal),"%.0f m",x);std::snprintf(vertical,sizeof(vertical),"%.0f",y);
        const auto horizontalSize=ImGui::GetFont()->CalcTextSizeA(fontSize,1e6f,0,horizontal);
        const auto verticalSize=ImGui::GetFont()->CalcTextSizeA(fontSize,1e6f,0,vertical);
        const float tx=origin.x+fraction*size.x,ty=origin.y+fraction*size.y;
        draw->AddLine(ImVec2(tx,end.y),ImVec2(tx,end.y+3),labelColor);
        AppUi::Widgets::drawText(draw,ImGui::GetFont(),fontSize,ImVec2((std::clamp)(tx-horizontalSize.x*.5f,origin.x,end.x-horizontalSize.x),end.y+4),labelColor,horizontal);
        draw->AddLine(ImVec2(origin.x-3,ty),ImVec2(origin.x,ty),labelColor);
        AppUi::Widgets::drawText(draw,ImGui::GetFont(),fontSize,ImVec2(origin.x-7-verticalSize.x,(std::clamp)(ty-fontSize*.5f,origin.y,end.y-fontSize)),labelColor,vertical);
    }
    const char* components[]={"Vz","Vx","Vy","|V|","Pressure"};
    char caption[64]{};std::snprintf(caption,sizeof(caption),"%s / %s",components[s.component3D],s.component3D==4?"Pa":"m/s");
    const auto captionSize=ImGui::GetFont()->CalcTextSizeA(fontSize,1e6f,0,caption);
    if(size.x>captionSize.x+16&&size.y>70) {
        draw->AddRectFilled(ImVec2(origin.x+5,origin.y+5),ImVec2(origin.x+captionSize.x+15,origin.y+fontSize+13),lightCanvas?IM_COL32(255,253,248,230):IM_COL32(7,13,20,205),3);
        AppUi::Widgets::drawText(draw,ImGui::GetFont(),fontSize,ImVec2(origin.x+10,origin.y+8),lightCanvas?IM_COL32(46,74,82,255):IM_COL32(201,219,230,255),caption);
    }
}
} // namespace

// Restore all orthogonal cameras without moving slice planes or modifying the simulated field.
void resetSliceViews3D(Seis3D_State& s) {
    for(auto& camera:s.sliceCameras3D) camera=SliceCamera3D{};
}

// Draw a full-resolution slice with centered whole-section zoom, captured panning and physical picking.
void drawSlicePanel3D(Seis3D_State& s,int plane) {
    AppUi::FunctionStyleScope colors(AppUi::FunctionRole::Display);
    ImGui::PushID(plane);
    const bool visible=ImGui::BeginChild("slicePanel",ImVec2(0,0),ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    if(!visible) {ImGui::EndChild();ImGui::PopID();return;}
    if(!s.ready3D) {
        AppUi::Widgets::TextWrapped("Initialize a 3D model to inspect this plane.");
        ImGui::EndChild();ImGui::PopID();return;
    }
    auto& camera=s.sliceCameras3D[plane];constrainSliceCamera3D(camera);
    const int axis=plane==0?1:plane==1?0:2;
    const int count=axis==0?s.context3D.nx3D:axis==1?s.context3D.ny3D:s.context3D.nz3D;
    ImGui::SetNextItemWidth(-1);
    if(AppUi::Widgets::SliderInt("##position",s.slice3D+axis,0,count-1,"%d",ImGuiSliderFlags_AlwaysClamp)) s.dirty3D=true;
    if(AppUi::Widgets::SmallButton("Reset view")) camera=SliceCamera3D{};
    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("Fit this slice to its panel. Keeps the current slice position and wavefield.");
    ImGui::SameLine();AppUi::Widgets::TextDisabled("%.0f%% | %c = %.0f m",camera.zoom3D*100,'X'+axis,
        s.slice3D[axis]*s.context3D.spacing3D);
    const int width=s.gl3D.layout3D.sliceWidth3D[plane],height=s.gl3D.layout3D.sliceHeight3D[plane];
    const auto available=ImGui::GetContentRegionAvail();
    if(available.x<70||available.y<50) {ImGui::EndChild();ImGui::PopID();return;}
    const auto& layout=s.gl3D.layout3D;
    const int offsetX=layout.domainOrigin3D[plane==1?1:0],offsetY=layout.domainOrigin3D[plane==2?1:2];
    const float ratio=s.showBoundary3D?float(width)/height:
        float(plane==1?s.context3D.ny3D:s.context3D.nx3D)/(plane==2?s.context3D.ny3D:s.context3D.nz3D);
    const float rulerWidth=AppUi::Widgets::CalcTextSize("-99999").x+10;
    const ImVec2 imageArea((std::max)(8.0f,available.x-rulerWidth),(std::max)(8.0f,available.y-ImGui::GetTextLineHeight()-8));
    const ImVec2 fittedSize=imageArea.x/imageArea.y>ratio?ImVec2(imageArea.y*ratio,imageArea.y):ImVec2(imageArea.x,imageArea.x/ratio);
    const ImVec2 size((std::min)(imageArea.x,fittedSize.x*camera.zoom3D),(std::min)(imageArea.y,fittedSize.y*camera.zoom3D));
    const auto cursor=ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x+rulerWidth+(imageArea.x-size.x)*.5f,cursor.y+(imageArea.y-size.y)*.5f));
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    // An ID-bearing canvas owns wheel input and continues a middle-button drag outside its bounds.
    ImGui::InvisibleButton("sliceCanvas3D",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const auto& io=ImGui::GetIO();
    const ImVec2 pointer((std::clamp)((io.MousePos.x-origin.x)/size.x,0.0f,1.0f),
        (std::clamp)((io.MousePos.y-origin.y)/size.y,0.0f,1.0f));
    if(hovered&&io.MouseWheel!=0) zoomSlice3D(camera,io.MouseWheel);
    if(active&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        camera.center3D[0]-=io.MouseDelta.x/(fittedSize.x*camera.zoom3D);
        camera.center3D[1]-=io.MouseDelta.y/(fittedSize.y*camera.zoom3D);
        constrainSliceCamera3D(camera);
    }
    const auto bounds=sliceBounds3D(s,plane,camera,ImVec2(size.x/(fittedSize.x*camera.zoom3D),size.y/(fittedSize.y*camera.zoom3D)));
    ImGui::GetWindowDrawList()->AddImage(ImTextureID(s.gl3D.displayTextures3D[plane]),origin,
        ImVec2(origin.x+size.x,origin.y+size.y),bounds.minimum3D,bounds.maximum3D);
    drawSliceOverlay3D(s,plane,bounds,origin,size);
    if(hovered) {
        int u=(std::clamp)(int((bounds.minimum3D.x+pointer.x*(bounds.maximum3D.x-bounds.minimum3D.x))*float(width)),0,width-1)+offsetX;
        int v=(std::clamp)(int((bounds.minimum3D.y+pointer.y*(bounds.maximum3D.y-bounds.minimum3D.y))*float(height)),0,height-1)+offsetY;
        if(!s.showBoundary3D) {
            u=(std::clamp)(u,0,(plane==1?s.context3D.ny3D:s.context3D.nx3D)-1);
            v=(std::clamp)(v,0,(plane==2?s.context3D.ny3D:s.context3D.nz3D)-1);
        }
        const Receiver3D point=plane==0?Receiver3D{u,s.slice3D[1],v}:plane==1?Receiver3D{s.slice3D[0],u,v}:Receiver3D{u,v,s.slice3D[2]};
        if(point.x3D<0||point.y3D<0||point.z3D<0||point.x3D>=s.context3D.nx3D||point.y3D>=s.context3D.ny3D||point.z3D>=s.context3D.nz3D) {
            s.hoverValid3D=false;
            AppUi::Widgets::SetTooltip("CPML absorbing layer | XYZ [%d,%d,%d]\nExternal damping cells; source/receivers remain inside the physical model.",point.x3D,point.y3D,point.z3D);
            ImGui::EndChild();ImGui::PopID();return;
        }
        if(!s.hoverValid3D||point.x3D!=s.hoverPoint3D.x3D||point.y3D!=s.hoverPoint3D.y3D||point.z3D!=s.hoverPoint3D.z3D)
            performAction3D(s,[&]{
                probeMaterial3D(s.gpu3D,s.context3D,point,s.hoverMaterial3D);
                s.hoverPoint3D=point;s.hoverValid3D=true;
            });
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            s.slice3D[0]=point.x3D;s.slice3D[1]=point.y3D;s.slice3D[2]=point.z3D;s.dirty3D=true;
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)&&!ImGui::IsAnyMouseDown())
            AppUi::Widgets::SetTooltip("Wheel: zoom entire section (100-3200%%)\nMiddle drag: pan | Left click: link slices\nReset view: fit this slice | R: reset all views");
    }
    ImGui::EndChild();ImGui::PopID();
}
} // namespace Seis3D::Detail3D
