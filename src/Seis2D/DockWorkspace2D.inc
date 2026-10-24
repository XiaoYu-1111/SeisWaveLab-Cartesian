// Private dock state and offscreen canvas; no CUDA solver or 3D state is shared here.
struct CanvasHandles2D {GLuint framebuffer=0,texture=0;int width=0,height=0;} canvasGl2D;
ImVec2 canvasOrigin2D{},canvasSize2D{800,600};
ImDrawList* canvasDrawList2D=nullptr;
bool canvasHovered2D=false,resetDockLayout2D=false;
bool selectExportTab2D=false,selectModelIoTab2D=false,focusControls2D=false;
int dockPreset2D=0;
constexpr const char* controlsName2D="Control Panel###Seis2D.Controls";
constexpr const char* canvasName2D="Wavefield X-Z###Seis2D.Canvas";
constexpr const char* recordsName2D="Seismic Data Analyzer###Seis2D.RecordViewer";

// Restrict dock targets to this module while preserving movable independent windows.
inline ImGuiWindowClass dockWindowClass2D() {
    ImGuiWindowClass result;result.ClassId=ImHashStr("Seis2D.WorkspaceClass");result.DockingAllowUnclassed=false;return result;
}
// Build only on initial entry or an explicit preset; normal user docking is retained in imgui.ini.
inline void prepareDockspace2D(int width,int height,float barHeight) {
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x,viewport->Pos.y+barHeight));
    ImGui::SetNextWindowSize(ImVec2(float(width),std::max(80.0f,height-2*barHeight)));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);
    ImGui::Begin("Seis2D.DockHost",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus);
    const ImGuiID id=ImGui::GetID("Seis2D.Dockspace.v4");const auto size=ImGui::GetContentRegionAvail();
    if(resetDockLayout2D||!ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderRemoveNode(ImGui::GetID("Seis2D.Dockspace.v1"));
        ImGui::DockBuilderRemoveNode(ImGui::GetID("Seis2D.Dockspace.v2"));
        ImGui::DockBuilderRemoveNode(id);ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id,size);ImGui::DockBuilderSetNodePos(id,ImGui::GetCursorScreenPos());
        ImGuiID center=id;const float ratio=std::clamp(500.0f/size.x,.25f,.45f);
        const auto controls=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,ratio,nullptr,&center);
        ImGui::DockBuilderDockWindow(AppUi::windowLabel(controlsName2D),controls);
        ImGui::DockBuilderDockWindow(AppUi::windowLabel("Current Parameters###Seis2D.Parameters"),controls);
        if(dockPreset2D==1) {
            const auto traces=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.48f,nullptr,&center);
            ImGui::DockBuilderDockWindow(AppUi::windowLabel(recordsName2D),traces);
        } else ImGui::DockBuilderDockWindow(AppUi::windowLabel(recordsName2D),dockPreset2D==2?center:controls);
        ImGui::DockBuilderDockWindow(AppUi::windowLabel(canvasName2D),center);
        ImGui::DockBuilderFinish(id);showHUD2D=true;
        if(resetDockLayout2D) recordViewer2D.isOpen=dockPreset2D!=0;
        resetDockLayout2D=false;
    }
    auto windowClass=dockWindowClass2D();recordViewer2D.dockingClass=windowClass.ClassId;
    ImGui::DockSpace(id,ImVec2(0,0),ImGuiDockNodeFlags_None,&windowClass);
    ImGui::End();ImGui::PopStyleVar(3);
}
// Let the top toolbar restore useful layouts without resetting gain, geometry or physical fields.
inline void drawWorkspaceMenu2D() {
    if(AppUi::Widgets::SmallButton("Workspace")) ImGui::OpenPopup("Seis2D.WorkspaceMenu");
    if(ImGui::BeginPopup("Seis2D.WorkspaceMenu")) {
        const char* names[]={"Modeling layout","Wavefield + records layout","Record focus layout"};
        for(int i=0;i<3;++i) if(AppUi::Widgets::MenuItem(names[i])) {dockPreset2D=i;resetDockLayout2D=true;}
        if(AppUi::Widgets::MenuItem("Show controls")) showHUD2D=true;
        if(AppUi::Widgets::MenuItem("Show records")) recordViewer2D.isOpen=true;
        ImGui::EndPopup();
    }
}
// Allocate canvas pixels at the current viewport resolution; resize never changes the physical grid.
inline void ensureCanvasTarget2D(int width,int height) {
    width=std::clamp(width,16,4096);height=std::clamp(height,16,4096);
    if(canvasGl2D.width==width&&canvasGl2D.height==height&&canvasGl2D.framebuffer) return;
    if(!canvasGl2D.framebuffer) glGenFramebuffers(1,&canvasGl2D.framebuffer);
    if(!canvasGl2D.texture) glGenTextures(1,&canvasGl2D.texture);
    glBindTexture(GL_TEXTURE_2D,canvasGl2D.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,canvasGl2D.framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,canvasGl2D.texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Cannot allocate the 2D viewport framebuffer.");
    canvasGl2D.width=width;canvasGl2D.height=height;
}
// Release the private offscreen image before the application destroys its GL context.
inline void releaseCanvasTarget2D() noexcept {
    if(canvasGl2D.framebuffer) glDeleteFramebuffers(1,&canvasGl2D.framebuffer);
    if(canvasGl2D.texture) glDeleteTextures(1,&canvasGl2D.texture);
    canvasGl2D={};canvasDrawList2D=nullptr;canvasHovered2D=false;
}
