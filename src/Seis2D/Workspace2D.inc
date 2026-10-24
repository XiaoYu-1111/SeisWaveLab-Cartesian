// Draw rulers and physical markers clipped to the actual docked image region.
inline void drawCanvasAnnotations2D(SimState& state,int winW,int winH) {
    const float barHeight=0;


    ViewportInfo vp;
    vp.x = canvasOrigin2D.x;
    vp.y = canvasOrigin2D.y;
    vp.w = (float)winW;
    vp.h = (float)winH;
    vp.scaleX = vp.w / (float)simulationContext2D.NX;
    vp.scaleY = vp.h / (float)simulationContext2D.NZ;
    renderBrushCursor(state, vp);

    renderSourceMarker(simulationContext2D, editSrcX2D, editSrcZ2D, winW, winH, barHeight, viewOffset2D, viewZoom2D);

    if (acquisition2D.isShowarray && !acquisition2D.receivers.empty()) {
        ImDrawList* fg = canvasDrawList2D;

        float  winAspect = (float)winW / (float)winH;
        float  simAspect = float(viewWidth2D())/viewHeight2D();
        ImVec2 aspectCorr = { 1.0f, 1.0f };
        if (winAspect > simAspect) {
            aspectCorr.x = winAspect / simAspect;
        }
        else {
            aspectCorr.y = simAspect / winAspect;
        }

        for (size_t i = 0; i < acquisition2D.receivers.size(); ++i) {
            float aPos_x = ((float(acquisition2D.receivers[i].x)-viewOrigin2D()) / viewWidth2D()) * 2.0f - 1.0f;
            float aPos_y = 1.0f - ((float(acquisition2D.receivers[i].y)-viewOriginZ2D()) / viewHeight2D()) * 2.0f;

            float Pndc_x = (aPos_x - viewOffset2D.x) * viewZoom2D / aspectCorr.x;
            float screenX = (Pndc_x * 0.5f + 0.5f) * winW;

            float Pndc_y = (aPos_y - viewOffset2D.y) * viewZoom2D / aspectCorr.y;
            float screenY = (0.5f - Pndc_y * 0.5f) * winH;

            if (screenX >= 0.0f && screenX <= winW && screenY >= barHeight && screenY <= winH) {

                ImVec2 p1(canvasOrigin2D.x+screenX, canvasOrigin2D.y+screenY - 5.0f);
                ImVec2 p2(canvasOrigin2D.x+screenX - 4.0f, canvasOrigin2D.y+screenY + 3.0f);
                ImVec2 p3(canvasOrigin2D.x+screenX + 4.0f, canvasOrigin2D.y+screenY + 3.0f);
                fg->AddCircleFilled(ImVec2(canvasOrigin2D.x+screenX,canvasOrigin2D.y+screenY),2.5f,IM_COL32(65,180,172,240));
            }
        }
    }

}

// Private implementation fragment, included once by Simulation2D.cpp.
// Draw grouped solver, model, source, acquisition and display controls with status bars.
inline void renderSeisHud(SimState& state, int winW, int winH, float barHeight, float scale, GLHandles& gl, const GpuInfo& info) {
    ImGuiIO& io = ImGui::GetIO();


    {
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ (float)winW, barHeight });

        ImVec4 topBarBg = AppUi::palette().background;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, topBarBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * scale, 0));

        ImGui::PushStyleColor(ImGuiCol_CheckMark, uiAccent2D);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, AppUi::palette().input);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(uiAccent2D.x * 0.15f, uiAccent2D.y * 0.15f, uiAccent2D.z * 0.15f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(uiAccent2D.x * 0.3f, uiAccent2D.y * 0.3f, uiAccent2D.z * 0.3f, 0.6f));

        AppUi::Widgets::Begin("TopBar_Seis", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::SetCursorPosY(std::max(0.0f,(barHeight-ImGui::GetTextLineHeight())*.5f));
            if(ImGui::BeginTable("Seis2D.StatusColumns",3,ImGuiTableFlags_SizingStretchProp)) {
                const auto& style=ImGui::GetStyle();
                const float actionWidth=AppUi::Widgets::CalcTextSize(AppUi::text("Home")).x
                    +AppUi::Widgets::CalcTextSize(AppUi::text("Record viewer")).x
                    +AppUi::Widgets::CalcTextSize(AppUi::text("Workspace")).x
                    +AppUi::Widgets::CalcTextSize(AppUi::text("OSD_PANEL")).x
                    +6*style.FramePadding.x+ImGui::GetFrameHeight()+5*style.ItemSpacing.x;
                ImGui::TableSetupColumn("Actions",ImGuiTableColumnFlags_WidthFixed,actionWidth);
                ImGui::TableSetupColumn("Device",ImGuiTableColumnFlags_WidthStretch,.55f);
                ImGui::TableSetupColumn("Progress",ImGuiTableColumnFlags_WidthStretch,.45f);
                ImGui::TableNextColumn();
                if(AppUi::Widgets::SmallButton("Home")) state.currentScreen=AppScreen::Intro0;
                ImGui::SameLine();if(AppUi::Widgets::SmallButton("Record viewer")) openCapturedRecord2D();
                ImGui::SameLine();drawWorkspaceMenu2D();
                ImGui::SameLine();AppUi::Widgets::Checkbox("OSD_PANEL",&showHUD2D);
                ImGui::TableNextColumn();AppUi::Widgets::TextColored(uiAccent2D,"Seis2D | %s",gpuData2D.cpu?"CPU / OpenMP":info.name);
                ImGui::TableNextColumn();AppUi::Widgets::Text("STEP %d | t %.4f s | %.0f FPS",currentIt2D,currentIt2D*simulationContext2D.dt,io.Framerate);
                ImGui::EndTable();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        ImDrawList* topDraw = ImGui::GetBackgroundDrawList();
        topDraw->AddLine(
            { 0, barHeight },
            { (float)winW, barHeight },
            IM_COL32(uiAccent2D.x * 255, uiAccent2D.y * 255, uiAccent2D.z * 255, 60),
            1.0f
        );
    }

    if (showHUD2D) {
        const float controlWidth=std::clamp(winW*.32f,480.0f,620.0f);
        ImGui::SetNextWindowPos({ 20*scale,80*scale }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ controlWidth,float(winH)-barHeight-100*scale }, ImGuiCond_FirstUseEver);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, AppUi::palette().panel);
        ImGui::PushStyleColor(ImGuiCol_Border, uiAccent2D);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

        ImGui::PushStyleColor(ImGuiCol_SliderGrab, uiAccent2D);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(uiAccent2D.x * 1.2f, uiAccent2D.y * 1.2f, uiAccent2D.z * 1.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TabActive, AppUi::palette().raised);
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(uiAccent2D.x * 0.8f, uiAccent2D.y * 0.8f, uiAccent2D.z * 0.8f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, AppUi::palette().raised);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(uiAccent2D.x * 0.8f, uiAccent2D.y * 0.8f, uiAccent2D.z * 0.8f, 0.8f));

        auto windowClass=dockWindowClass2D();ImGui::SetNextWindowClass(&windowClass);
        if(focusControls2D) {ImGui::SetNextWindowFocus();focusControls2D=false;}
        if (AppUi::Widgets::Begin(controlsName2D, &showHUD2D)) {
            AppUi::drawLanguageSelector(-1);
            static float displayFPS = 60.0f;
            displayFPS = displayFPS * 0.95f + io.Framerate * 0.05f;
            AppUi::Widgets::TextColored(uiAccent2D, "FPS: %.1f", displayFPS);
            ImGui::Separator();

            drawParameterGroupTools2D();
            drawPerformanceControls2D(state);
            if (ImGui::BeginTabBar("ControlTabs")) {

                if (AppUi::Widgets::BeginTabItem("Simulation")) {
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Simulation);
                    ImGui::BeginDisabled(acquisition2D.isRecording);
                    ImGui::Spacing();
                    if(parameterGroup2D("Simulation Protocol")) {

                    const char* sim_types[] = { "Type 1: Whole-domain PML", "Type 2: Interior FDM + Boundary PML", "Type 3: Free Surface + PML" };
                    int temp_type = simulationContext2D.flag_type - 1;
                    if (AppUi::Widgets::Combo("Formulation", &temp_type, sim_types, IM_ARRAYSIZE(sim_types))) {
                        simulationContext2D.flag_type = temp_type + 1;
                        if (simulationContext2D.flag_type == 3 && simulationContext2D.upFlag && simulationContext2D.npml < kStencilRadius) {
                            simulationContext2D.npml = kStencilRadius;
                            tempPml2D = kStencilRadius;
                            parameters2D.FDM.npml = static_cast<float>(kStencilRadius);
                            updatePmlDampingArrays();
                        }
                        resetSimRequested2D = true;
                    }
                    ImGui::Spacing();

                    }
                    if(parameterGroup2D("Time Step, Grid Spacing & Iteration Control")) {

                    // Material edits refresh this cache; avoid a full grid scan every UI frame.
                    const float max_vp = cachedMaxVp2D;

                    AppUi::Widgets::SliderFloat("Time Step (dt)", &tempDt2D, 0.00001f, 0.003f, "%.6f s");
                    AppUi::Widgets::SliderFloat("Grid Spacing (dx)", &tempDx2D, 0.1f, 20.0f, "%.2f m");

                    const int minimumPmlWidth = simulationContext2D.flag_type == 3 && simulationContext2D.upFlag ? kStencilRadius : 0;
                    const int maximumPmlWidth = std::min(100, (std::min(simulationContext2D.NX, simulationContext2D.NZ) - 2 * kStencilRadius - 1) / 2);
                    tempPml2D = std::clamp(tempPml2D, minimumPmlWidth, maximumPmlWidth);
                    if (tempPml2D == 0) {
                        AppUi::Widgets::SliderInt("PML Layer Width", &tempPml2D, minimumPmlWidth, maximumPmlWidth, "0 (Total Reflection)");
                    }
                    else {
                        AppUi::Widgets::SliderInt("PML Layer Width", &tempPml2D, minimumPmlWidth, maximumPmlWidth, "%d px");
                    }
                    AppUi::Widgets::SliderInt("Max Steps (nt)", &tempNt2D, 500, 50000);
                    AppUi::Widgets::SliderInt("Steps / Frame", &stepsPerFrame2D, 1, 100);

                    float cfl_theory = kCourantLimit2d;
                    float dt_theory_limit = cfl_theory * tempDx2D / max_vp;

                    float cfl_safe = kSafeCourant2d;
                    float dt_safe_limit = cfl_safe * tempDx2D / max_vp;

                    AppUi::Widgets::TextDisabled("CFL Stability Analysis:");

                    AppUi::Widgets::Text("CFL %.3f | Limit dt %.6f s", cfl_theory, dt_theory_limit);
                    AppUi::Widgets::Text("CFL %.3f | Safe dt %.6f s", cfl_safe, dt_safe_limit);
                    AppUi::Widgets::Text("Draft dt %.6f s", tempDt2D);
                    if (tempDt2D <= dt_safe_limit)
                        AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Simulation), "Within the safe time step");
                    else if (tempDt2D <= dt_theory_limit)
                        AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Warning), "Above safe dt: reduce the time step");
                    else
                        AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Danger), "Unstable time step: reduce dt");

                    ImGui::Spacing();
                    if (AppUi::Widgets::Button("APPLY TIME & SPACING CONTROLS", { -1, 30 * scale })) {

                        applyPendingParameters2D(state,false);
                    }
                    ImGui::Spacing();

                    ImGui::Separator();
                    }
                    if(parameterGroup2D("GRID DIMENSIONS (NX, NZ)")) {

                    ImGui::PushItemWidth(100 * scale);
                    AppUi::Widgets::InputInt("##WidthVal", &gridWidth2D, 0, 0); ImGui::SameLine();
                    ImGui::PopItemWidth();
                    if (AppUi::Widgets::Button("-##WDec")) { gridWidth2D = std::max(64, gridWidth2D - 64); } ImGui::SameLine();
                    if (AppUi::Widgets::Button("+##WInc")) { gridWidth2D = std::min(8192, gridWidth2D + 64); } ImGui::SameLine();
                    AppUi::Widgets::Text("Grid Width (NX)");
                    ImGui::Spacing();

                    ImGui::PushItemWidth(100 * scale);
                    AppUi::Widgets::InputInt("##HeightVal", &gridHeight2D, 0, 0); ImGui::SameLine();
                    ImGui::PopItemWidth();
                    if (AppUi::Widgets::Button("-##HDec")) { gridHeight2D = std::max(64, gridHeight2D - 64); } ImGui::SameLine();
                    if (AppUi::Widgets::Button("+##HInc")) { gridHeight2D = std::min(8192, gridHeight2D + 64); } ImGui::SameLine();
                    AppUi::Widgets::Text("Grid Height (NZ)");

                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                    if (AppUi::Widgets::Button("APPLY NEW GRID SIZE & CLEAR MODEL", { -1, 35 * scale })) {
                        state.running = false;
                        currentIt2D = 0;
                        elapsedRunWallTime2D = 0.0f;
                        activeSources2D.clear();

                        parameters2D.model.xnum = gridWidth2D;
                        parameters2D.model.znum = gridHeight2D;

                        setupTestContext(simulationContext2D, parameters2D);
                        releaseGpuSimulation(gpuData2D);

                        if(gl.cudaSeisRes) cudaGraphicsUnregisterResource(gl.cudaSeisRes);gl.cudaSeisRes=nullptr;
                        glBindTexture(GL_TEXTURE_2D, gl.seisTex);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, simulationContext2D.NX+2*domainPad2D(), simulationContext2D.NZ+2*domainPad2D(), 0, GL_RGBA, GL_FLOAT, nullptr);


                        initializeExternalSimulation2D(gpuData2D, simulationContext2D);
                        firstAlignNeeded2D = true;
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::Spacing();
                    ImGui::Separator();

                    }
                                        ImGui::EndDisabled();
if(parameterGroup2D("Execution and interaction")) {
                    AppUi::Widgets::Checkbox("V-Sync Control", &state.vsyncEnabled);
                    if (ImGui::IsItemDeactivatedAfterEdit()) glfwSwapInterval(state.vsyncEnabled ? 1 : 0);
                    ImGui::Spacing();

                    AppUi::Widgets::Checkbox("Continuous Simulation (Infinite Mode)", &infiniteMode2D);
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Check to run the wavefield tank indefinitely.\nIdeal for interactive sandbox clicking.");
                    }
                    ImGui::Spacing();

                    AppUi::Widgets::Checkbox("Enable Multi-Source Real-time Clicks", &multiSourceMode2D);
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Check to click and spawn multiple intersecting waves simultaneously\nUncheck for standard single-point reset mode.");
                    }
                    AppUi::Widgets::Checkbox("Enable Show Monitor par", &showMonitorPar2D);
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Show additional diagnostic window for PML parameters and geophysical range.");
                    }

                    ImGui::Separator();
                    AppUi::Widgets::TextColored(uiAccent2D, "EXECUTION CONTROLS");

                    if (state.running) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.6f, 0.1f, 0.85f));
                        if (AppUi::Widgets::Button("PAUSE SIMULATION", { -1, 30 * scale })) {
                            state.running = false;
                        }
                        ImGui::PopStyleColor();
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.3f, 0.85f));
                        if (AppUi::Widgets::Button("RUN SIMULATION", { -1, 30 * scale })) {
                            state.running = true;
                        }
                        ImGui::PopStyleColor();
                    }

                    if (AppUi::Widgets::Button("RESET SIMULATION", { -1, 28 * scale })) {
                        resetSimRequested2D = true;
                    }

                    if (AppUi::Widgets::Button("RESET VIEWPORT", { -1, 28 * scale })) {
                        resetViewportRequested2D = true;
                    }

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiAccent2D.x * 0.12f, uiAccent2D.y * 0.32f, uiAccent2D.z * 0.42f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiAccent2D.x * 0.18f, uiAccent2D.y * 0.52f, uiAccent2D.z * 0.62f, 1.0f));
                    if (AppUi::Widgets::Button("OPEN DATA ANALYZER", { -1, 28 * scale })) {
                        recordViewer2D.isOpen = true; snapshotCaptureForViewer2D();
                    }
                    ImGui::PopStyleColor(2);
                    }
                                        ImGui::EndTabItem();
                }

                if (AppUi::Widgets::BeginTabItem("Source")) {
                    ImGui::BeginDisabled(acquisition2D.isRecording);
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Source);
                    ImGui::Spacing();
                    if(parameterGroup2D("Source Position, Type & Amplitude")) {


                    int min_valid_grid = 1;
                    int max_valid_x = simulationContext2D.NX - 2;
                    int max_valid_z = simulationContext2D.NZ - 2;

                    AppUi::Widgets::SliderInt("Source X (Grid)", &editSrcX2D, min_valid_grid, max_valid_x);
                    AppUi::Widgets::SliderInt("Source Z (Grid)", &editSrcZ2D, min_valid_grid, max_valid_z);
                    ImGui::Spacing();

                    AppUi::Widgets::SliderFloat("Excitation Amp", &editAmp2D, 0.1f, 10.0f, "%.1f x");
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Adjust the source intensity in real-time.");
                    }

                    const char* source_types[] = {
                        "Vertical body force (Vz)",
                        "Horizontal body force (Vx)",
                        "Isotropic stress (Txx + Tzz)",
                        "Shear stress (Txz)",
                        "Rotated body force"
                    };
                    AppUi::Widgets::Combo("Source Type", &editSourceType2D, source_types, IM_ARRAYSIZE(source_types));
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Select seismic source mechanism (e.g. Pure P-Wave or Shear S-Wave).");
                    }

                    if (editSourceType2D == 4) {
                        AppUi::Widgets::SliderFloat("Force Angle", &editAngle2D, -180.0f, 180.0f, "%.1f deg");
                    }

                    ImGui::Separator();
                    }
                    if(parameterGroup2D("Ricker Wavelet Properties")) {

                    AppUi::Widgets::SliderFloat("Peak Freq (f0)", &sourceFrequency2D, 5.0f, 300.0f, "%.1f Hz");
                    AppUi::Widgets::SliderFloat("Time Delay (t0)", &sourceDelay2D, 0.001f, 0.2f, "%.3f s");

                    AppUi::Widgets::TextDisabled("Waveform Real-Time Preview:");
                    float              preview_dt = 0.0005f;
                    std::vector<float> preview_x(200);
                    std::vector<float> preview_y(200);
                    for (int i = 0; i < 200; ++i) {
                        float t = i * preview_dt - sourceDelay2D;
                        float pi2_f0 = M_PI * M_PI * sourceFrequency2D * sourceFrequency2D;
                        preview_x[i] = i * preview_dt;
                        preview_y[i] = (1.0f - 2.0f * pi2_f0 * t * t) * expf(-pi2_f0 * t * t) * editAmp2D;
                    }

                    ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(125, 125, 125, 255));
                    ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(125, 125, 125, 255));
                    ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(0, 255, 120, 255));
                    ImPlot::PushStyleColor(ImPlotCol_AxisGrid, IM_COL32(35, 35, 35, 120));

                    if (ImPlot::BeginPlot("##WaveletPreview", ImVec2(-1, 95 * scale), ImPlotFlags_NoLegend)) {
                        ImPlot::SetupAxes("Time", "Amp", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                        ImPlot::SetupAxesLimits(0.0f, 200 * preview_dt, -1.1f * editAmp2D, 1.1f * editAmp2D);
                        ImPlot::PlotLine("Ricker", preview_x.data(), preview_y.data(), 200);
                        ImPlot::EndPlot();
                    }
                    ImPlot::PopStyleColor(4);

                    ImGui::Spacing();
                    if (AppUi::Widgets::Button("APPLY SOURCE CONFIG", { -1, 30 * scale })) {
                        simulationContext2D.src_z_idx = editSrcZ2D;
                        simulationContext2D.src_idx = editSrcZ2D * simulationContext2D.NX + editSrcX2D;
                        simulationContext2D.src_angle = editAngle2D;
                        simulationContext2D.src_type = editSourceType2D;
                        simulationContext2D.src_amp = editAmp2D;

                        parameters2D.FDM.f0 = sourceFrequency2D;
                        parameters2D.FDM.t0 = sourceDelay2D;
                        parameters2D.FDM.angle = editAngle2D;

                        generateRickerWavelet(simulationContext2D.wavelet, simulationContext2D.nt, simulationContext2D.dt, sourceFrequency2D, sourceDelay2D);
                        currentIt2D = 0;
                        state.running = false;
                        elapsedRunWallTime2D = 0.0f;

                        releaseGpuSimulation(gpuData2D);
                        initializeExternalSimulation2D(gpuData2D, simulationContext2D);
                    }
                    ImGui::Spacing();
                    if (AppUi::Widgets::Button("TRIGGER SINGLE SOURCE SHOT (C)", { -1, 30 * scale })) {
                        multiSourceMode2D = false;
                        simulationContext2D.src_z_idx = editSrcZ2D;
                        simulationContext2D.src_idx = editSrcZ2D * simulationContext2D.NX + editSrcX2D;
                        simulationContext2D.src_type = editSourceType2D;
                        simulationContext2D.src_amp = editAmp2D;
                        resetSimRequested2D = true;
                    }
                    ImGui::Spacing();
                    }
                                        ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }

                if (AppUi::Widgets::BeginTabItem("Model Edit")) {
                    ImGui::BeginDisabled(acquisition2D.isRecording);
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Model);
                    ImGui::Spacing();
                    if(parameterGroup2D("Rock Physics Coupling")) {


                    AppUi::Widgets::SliderFloat("P-Wave Velocity (Vp)", &editVp2D, 500.0f, 6000.0f, "%.1f m/s");
                    AppUi::Widgets::SliderFloat("S-Wave Velocity (Vs)", &editVs2D, 200.0f, 3500.0f, "%.1f m/s");
                    AppUi::Widgets::SliderFloat("Density (Rho)", &editDensity2D, 1000.0f, 3000.0f, "%.1f kg/m^3");

                    if (editVp2D < editVs2D * 1.5f) editVp2D = editVs2D * 1.5f;

                    ImGui::Spacing();
                    if (AppUi::Widgets::Button("APPLY PHYSICAL MEDIUM", { -1, 30 * scale })) {
                        float mu_val = editDensity2D * editVs2D * editVs2D;
                        float lambda2mu_val = editDensity2D * editVp2D * editVp2D;
                        float lambda_val = lambda2mu_val - 2.0f * mu_val;

                        simulationContext2D.rho.assign(simulationContext2D.total_grid, editDensity2D);
                        simulationContext2D.mu.assign(simulationContext2D.total_grid, mu_val);
                        simulationContext2D.lambda.assign(simulationContext2D.total_grid, lambda_val);
                        simulationContext2D.lambda2mu.assign(simulationContext2D.total_grid, lambda2mu_val);
                        resetSimRequested2D = true;
                        updatePmlDampingArrays();
                        autoAlignTimeStep();
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    }
                    if(parameterGroup2D("GEOPHYSICAL SCENARIO PRESETS")) {


                    const char* scene_names[] = {
                        "Uniform Medium",
                        "Earth Shell & Core",
                        "Double Slit Interference",
                        "2-Layered Medium",
                        "Straight Waveguide",
                        "Curved Waveguide",
                        "Phononic Crystal Hex",
                        "Random Scattering",
                        "Sinusoidal Interface",
                        "Linear Velocity Gradient",
                        "Penrose Room",
                        "SCENE_TOPOGRAPHY"
                    };

                    static int selected_scene = currentScene2D;
                    AppUi::Widgets::Combo("Select Preset", &selected_scene, scene_names, IM_ARRAYSIZE(scene_names));

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiAccent2D.x * 0.15f, uiAccent2D.y * 0.5f, uiAccent2D.z * 0.4f, 0.75f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiAccent2D.x * 0.2f, uiAccent2D.y * 0.7f, uiAccent2D.z * 0.5f, 0.9f));
                    ImGui::Spacing();
                    if (AppUi::Widgets::Button("APPLY SCENARIO & REALLOCATE", { -1, 32 * scale })) {
                        currentScene2D = selected_scene;
                        applyScenario(currentScene2D, state);

                        gridWidth2D = simulationContext2D.NX;
                        gridHeight2D = simulationContext2D.NZ;

                        if(gl.cudaSeisRes) cudaGraphicsUnregisterResource(gl.cudaSeisRes);gl.cudaSeisRes=nullptr;
                        glBindTexture(GL_TEXTURE_2D, gl.seisTex);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, simulationContext2D.NX+2*domainPad2D(), simulationContext2D.NZ+2*domainPad2D(), 0, GL_RGBA, GL_FLOAT, nullptr);


                        firstAlignNeeded2D = true;
                        resetSimRequested2D = true;
                    }
                    ImGui::Spacing();

                    ImGui::PopStyleColor(2);
                    }
                    if(parameterGroup2D("Mouse Interaction Tool")) {


                    const char* brush_names[] = {
                        "1. Default (Trigger Source�� ",
                        "2. High Velocity Brush ",
                        "3. Low Velocity Brush ",
                        "4. Custom Material Brush ",
                        "5. Eraser "
                    };

                    int current_tool = state.brushType;
                    ImGui::PushItemWidth(-1);

                    if (AppUi::Widgets::Combo("##LeftClickToolCombo", &current_tool, brush_names, IM_ARRAYSIZE(brush_names))) {
                        state.brushType = current_tool;
                    }
                    ImGui::PopItemWidth();

                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Choose what happens when you left-click on the wavefield.\nOr use hotkeys [1] to [5] on your keyboard.");
                    }

                    if (state.brushType != TOOL_NONE) {
                        ImGui::Spacing();

                        AppUi::Widgets::SliderFloat("Brush Radius (px)", &state.brushRadius, 1.0f, 50.0f, "Radius: %.0f px");
                        if (ImGui::IsItemHovered()) {
                            AppUi::Widgets::SetTooltip("Adjust the physical size of your paintbrush circle.");
                        }
                    }
                    ImGui::Spacing();
                    }
                                        ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }

                if (AppUi::Widgets::BeginTabItem("Model I/O",nullptr,selectModelIoTab2D?ImGuiTabItemFlags_SetSelected:0)) {
                    selectModelIoTab2D=false;
                    ImGui::BeginDisabled(acquisition2D.isRecording);
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Model);

                    drawVtiModelControls2D(gl,state);
                    ImGui::Separator();
                    if(parameterGroup2D("EXTERNAL MODEL IMPORT (SEG-Y GROUP)",false)) {


                    static char vp_file_path[512] = "";
                    static char vs_file_path[512] = "";
                    static char rho_file_path[512] = "";

                    AppUi::Widgets::InputText("Vp (.sgy)", vp_file_path, IM_ARRAYSIZE(vp_file_path));
                    ImGui::SameLine();
                    if (AppUi::Widgets::Button("Browse##Vp", ImVec2(65 * scale, 0))) {
                        char filePath[512] = { 0 };
                        if (openSystemFileDialog(filePath, sizeof(filePath))) {
                            strcpy_s(vp_file_path, filePath);
                        }
                    }

                    AppUi::Widgets::InputText("Vs (.sgy)", vs_file_path, IM_ARRAYSIZE(vs_file_path));
                    ImGui::SameLine();
                    if (AppUi::Widgets::Button("Browse##Vs", ImVec2(65 * scale, 0))) {
                        char filePath[512] = { 0 };
                        if (openSystemFileDialog(filePath, sizeof(filePath))) {
                            strcpy_s(vs_file_path, filePath);
                        }
                    }

                    AppUi::Widgets::InputText("Rho(.sgy)", rho_file_path, IM_ARRAYSIZE(rho_file_path));
                    ImGui::SameLine();
                    if (AppUi::Widgets::Button("Browse##Rho", ImVec2(65 * scale, 0))) {
                        char filePath[512] = { 0 };
                        if (openSystemFileDialog(filePath, sizeof(filePath))) {
                            strcpy_s(rho_file_path, filePath);
                        }
                    }

                    AppUi::Widgets::SliderFloat("Original SEGY Spacing (dx)", &editSegyDx2D, 0.01f, 50.0f, "%.3f m");
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("The original spatial spacing of your loaded SGY files.\nExample: 12.5m for standard Marmousi.");
                    }

                    static bool flipSegyY = true;
                    static bool flipSegyX = false;
                    AppUi::Widgets::Checkbox("if (rho > 0.0f && rho < 10.0f) rho *= 1000.0f;", &convertRho2D);
                    AppUi::Widgets::Checkbox("Flip Vertically on SEGY Import", &flipSegyY);
                    AppUi::Widgets::Checkbox("Flip Horizontally on SEGY Import", &flipSegyX);
                    ImGui::Spacing();

                    if (AppUi::Widgets::Button("LOAD SEGY MODEL GROUP", { -1, 35 * scale })) {
                        if (strlen(vp_file_path) > 0 && strlen(vs_file_path) > 0 && strlen(rho_file_path) > 0) {
                            if (loadModelFromSegy(vp_file_path, vs_file_path, rho_file_path, flipSegyY, flipSegyX, gl, state)) {
                                popupMessage2D = "SEGY Model Group loaded successfully!";
                                showSuccessPopup2D = true;
                                resetSimRequested2D = true;
                            }
                            else {
                                popupMessage2D = "Failed to load SEGY files.\nPlease check file formatting or grid dimensions.";
                                showErrorPopup2D = true;
                            }
                        }
                        else {
                            popupMessage2D = "Error: Please select all three SGY files (Vp, Vs, Rho)!";
                            showErrorPopup2D = true;
                        }
                    }

                    ImGui::Separator();
                    }
                    if(parameterGroup2D("EXPORT CURRENT MODEL TO SEG-Y GROUP",false)) {


                    static bool export_density_gcm3 = true;
                    static bool export_flip_x = false;

                    static int crop_x_min = 0;
                    static int crop_z_min = 0;
                    static int crop_x_max = 0;
                    static int crop_z_max = 0;

                    if (crop_x_max <= 0 || crop_x_max >= simulationContext2D.NX || firstAlignNeeded2D) {
                        crop_x_min = 0;
                        crop_z_min = 0;
                        crop_x_max = simulationContext2D.NX - 1;
                        crop_z_max = simulationContext2D.NZ - 1;
                    }

                    if (firstAlignNeeded2D) {
                        exportTargetDx2D = simulationContext2D.h;
                    }

                    AppUi::Widgets::Checkbox("Convert Density to g/cm3 on Export", &export_density_gcm3);
                    AppUi::Widgets::Checkbox("Flip Horizontally on Export", &export_flip_x);

                    AppUi::Widgets::TextDisabled("Crop Sub-Region Coordinates (Grid Units):");
                    AppUi::Widgets::SliderInt("X Min (Left)", &crop_x_min, 0, simulationContext2D.NX - 1);
                    AppUi::Widgets::SliderInt("X Max (Right)", &crop_x_max, 0, simulationContext2D.NX - 1);
                    AppUi::Widgets::SliderInt("Z Min (Top)", &crop_z_min, 0, simulationContext2D.NZ - 1);
                    AppUi::Widgets::SliderInt("Z Max (Bottom)", &crop_z_max, 0, simulationContext2D.NZ - 1);

                    AppUi::Widgets::SliderFloat("Export Target dx (m)", &exportTargetDx2D, 0.1f, 20.0f, "%.2f m");
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Target spatial spacing for exported SGY grid.\nSetting this different from active dx will trigger 2D Resampling.\ne.g., downsample Marmousi from 1.5m to 2.5m.");
                    }

                    float sub_btn_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

                    if (AppUi::Widgets::Button("SET TO FULL GRID", ImVec2(sub_btn_w, 0))) {
                        crop_x_min = 0;
                        crop_z_min = 0;
                        crop_x_max = simulationContext2D.NX - 1;
                        crop_z_max = simulationContext2D.NZ - 1;
                    }
                    ImGui::SameLine();

                    if (AppUi::Widgets::Button("STRIP PML BOUNDARY", ImVec2(sub_btn_w, 0))) {
                        crop_x_min = 0;
                        crop_z_min = 0;
                        crop_x_max = simulationContext2D.NX - 1;
                        crop_z_max = simulationContext2D.NZ - 1;
                    }

                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiAccent2D.x * 0.15f, uiAccent2D.y * 0.45f, uiAccent2D.z * 0.55f, 0.75f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiAccent2D.x * 0.2f, uiAccent2D.y * 0.65f, uiAccent2D.z * 0.75f, 0.9f));
                    if (AppUi::Widgets::Button("EXPORT SEG-Y GROUP", { -1, 35 * scale })) {
                        char savePath[512] = { 0 };
                        if (saveSystemFileDialog(savePath, sizeof(savePath))) {

                            if (exportModelToSegy(savePath, export_density_gcm3, export_flip_x, crop_x_min, crop_z_min, crop_x_max, crop_z_max, exportTargetDx2D)) {
                                popupMessage2D = "Cropped & Resampled SEGY Model Group exported successfully!\nCheck out your saved _vp, _vs, _rho files.";
                                showSuccessPopup2D = true;
                            }
                            else {
                                popupMessage2D = "Failed to export model.\nPlease check target path permissions.";
                                showErrorPopup2D = true;
                            }
                        }
                    }
                    ImGui::Spacing();
                    ImGui::PopStyleColor(2);
                    }
                                        ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }

                if (AppUi::Widgets::BeginTabItem("Data Export",nullptr,selectExportTab2D?ImGuiTabItemFlags_SetSelected:0)) {
                    selectExportTab2D=false;
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Acquisition);
                    ImGui::BeginDisabled(acquisition2D.isRecording);
                    ImGui::Spacing();

                    drawExportDestination2D();
                    if(parameterGroup2D("Acquisition Settings")) {

                    double intervalMs=1000.0/acquisition2D.samplingRateHz;
                    bool intervalChanged=AppUi::Widgets::InputDouble("Sampling interval (ms)",&intervalMs,0.1,1.0,"%.3f");
                    if(AppUi::Widgets::Button("0.5 ms##sample2D")) {intervalMs=0.5;intervalChanged=true;}
                    ImGui::SameLine();
                    if(AppUi::Widgets::Button("1 ms##sample2D")) {intervalMs=1.0;intervalChanged=true;}
                    if(intervalChanged&&std::isfinite(intervalMs)) {
                        intervalMs=(std::clamp)(intervalMs,0.1,100.0);
                        acquisition2D.samplingRateHz=float(1000.0/intervalMs);
                    }
                    AppUi::Widgets::SliderFloat("Sampling Rate (Hz)", &acquisition2D.samplingRateHz, 10.0f, 10000.0f, "%.0f Hz");
                    const int previewStride=getReceiverSamplingStride(simulationContext2D.dt,acquisition2D.samplingRateHz);
                    const double previewInterval=double(simulationContext2D.dt)*previewStride;
                    AppUi::Widgets::Text("Actual sample interval: %.3f ms",previewInterval*1000.0);
                    AppUi::Widgets::TextWrapped("Sampling interval is rounded to whole simulation steps; it cannot be shorter than one step.");
                    AppUi::Widgets::DragFloat("Record Duration (s)", &acquisition2D.totalDurationSec, 1.0f, 0.1f, 7200.0f, "%.2f s");

                    const double expectedPoints=std::ceil(acquisition2D.totalDurationSec/previewInterval);
                    AppUi::Widgets::TextColored(AppUi::functionColor(AppUi::FunctionRole::Acquisition),
                        "Requested samples: %.0f / trace",std::isfinite(expectedPoints)?expectedPoints:0);
                    ImGui::Spacing();

                    }
                    if(parameterGroup2D("Receiver Geometry")) {

                    AppUi::Widgets::SliderInt("Channels", &acquisition2D.numChannels, 1, 200, "%d");

                    ImGui::DragIntRange2("X Range", &acquisition2D.startX, &acquisition2D.endX, 1.0f, 0, simulationContext2D.NX - 1, "Start: %d", "End: %d");
                    AppUi::Widgets::SliderInt("Depth (Y)", &acquisition2D.receiverDepth, 0, simulationContext2D.NZ - 1);

                    if (AppUi::Widgets::Button("DEPLOY LINEAR ARRAY", ImVec2(-1, 0))) {
                        acquisition2D.receivers.clear();
                        acquisition2D.initBuffer();

                        float rangeX = (float)(acquisition2D.endX - acquisition2D.startX);
                        float spacing = (acquisition2D.numChannels > 1) ? (rangeX / (acquisition2D.numChannels - 1)) : 0;

                        for (int i = 0; i < acquisition2D.numChannels; ++i) {
                            acquisition2D.receivers.push_back({ (int)(acquisition2D.startX + i * spacing), acquisition2D.receiverDepth });
                        }

                        syncReceiversToGpu();
                    }
                    AppUi::Widgets::Checkbox("Show/Hide LINEAR ARRAY", &acquisition2D.isShowarray);
                    ImGui::Spacing();
                    }
                                        ImGui::EndDisabled();

                    if(parameterGroup2D("Recording Control")) {


                    if (acquisition2D.isRecording) {
                        float progress = acquisition2D.totalTimer / acquisition2D.totalDurationSec;
                        char  progBuf[64];
                        sprintf(progBuf, "Progress: %.2f / %.2f s", acquisition2D.totalTimer, acquisition2D.totalDurationSec);
                        ImGui::ProgressBar(progress, ImVec2(-1, 30), progBuf);

                        if (AppUi::Widgets::Button("STOP & SAVE NOW", ImVec2(-1, 40))) {
                            acquisition2D.isRecording = false;
                            state.running = false;

                            acquisitionAction2D([]{exportCapturePackage2D();});
                        }
                    }
                    else {
                        bool ready = !acquisition2D.receivers.empty() && (acquisition2D.receivers.size() == acquisition2D.numChannels);
                        if (!ready) ImGui::BeginDisabled();

                        float availW = ImGui::GetContentRegionAvail().x;
                        float spacingW = ImGui::GetStyle().ItemSpacing.x;

                        if (AppUi::Widgets::Button("START CAPTURE", ImVec2(availW * 0.4f, 45))) {
                            acquisitionAction2D([&]{
                            acquisition2D.totalTimer = 0.0f;
                            acquisition2D.currentFileIndex = 1;
                            beginExportSession2D();
                            acquisition2D.initBuffer();

                            syncReceiversToGpu();
                            acquisition2D.isRecording = true;
                            state.running = true;
                            });
                        }
                        ImGui::SameLine();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (AppUi::Widgets::Button("TRIGGER & ACQUIRE", ImVec2(availW * 0.6f - spacingW, 45))) {
                            acquisitionAction2D([&]{
                            acquisition2D.totalTimer = 0.0f;
                            acquisition2D.currentFileIndex = 1;
                            beginExportSession2D();
                            acquisition2D.initBuffer();

                            triggerSingleShot(gl, state);

                            acquisition2D.isRecording = true;
                            state.running = true;
                            });
                        }
                        ImGui::PopStyleColor(2);

                        if (!ready) {
                            ImGui::EndDisabled();
                            AppUi::Widgets::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Hint: Deploy receivers first.");
                        }
                    }

                    ImGui::BeginDisabled(acquisition2D.isRecording || acquisition2D.capturedSamples<1);
                    if(AppUi::Widgets::Button("Export current shot package",ImVec2(-1,0)))
                        acquisitionAction2D([]{exportCapturePackage2D();});
                    ImGui::EndDisabled();

                    }
                    if(parameterGroup2D("Automated Multi-Shot Gather")) {


                    AppUi::Widgets::SliderInt("Total Shots", &autoAcquisition2D.totalShots, 1, 100);

                    if (AppUi::Widgets::SliderInt("Start Shot X (Grid)", &autoAcquisition2D.startX, 1, simulationContext2D.NX - 2)) {
                        if (autoAcquisition2D.state == AUTO_ACQ_IDLE) {
                            editSrcX2D = autoAcquisition2D.startX;
                        }
                    }

                    if (AppUi::Widgets::SliderInt("Shot Depth Z (Grid)", &autoAcquisition2D.shotDepthZ, 1, simulationContext2D.NZ - 2)) {
                        if (autoAcquisition2D.state == AUTO_ACQ_IDLE) {
                            editSrcZ2D = autoAcquisition2D.shotDepthZ;
                        }
                    }

                    AppUi::Widgets::SliderInt("Shot Spacing (Grid)", &autoAcquisition2D.shotIntervalX, 5, 200);

                    int end_shot_x = autoAcquisition2D.startX + (autoAcquisition2D.totalShots - 1) * autoAcquisition2D.shotIntervalX;
                    if (end_shot_x >= simulationContext2D.NX - 2) {
                        AppUi::Widgets::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: End Shot X (%d) exceeds right boundary!", end_shot_x);
                    }
                    else {
                        AppUi::Widgets::TextDisabled("Estimated End Shot X: %d (Within Boundary)", end_shot_x);
                    }

                    ImGui::Spacing();

                    if (autoAcquisition2D.state == AUTO_ACQ_IDLE) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiAccent2D.x * 0.10f, uiAccent2D.y * 0.20f, uiAccent2D.z * 0.30f, 0.65f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiAccent2D.x * 0.15f, uiAccent2D.y * 0.35f, uiAccent2D.z * 0.45f, 0.85f));
                        if (AppUi::Widgets::Button("PREVIEW & SYNC POSITION", ImVec2(-1, 26 * scale))) {
                            editSrcX2D = autoAcquisition2D.startX;
                            editSrcZ2D = autoAcquisition2D.shotDepthZ;

                            triggerSingleShot(gl, state);
                        }
                        ImGui::PopStyleColor(2);
                        ImGui::Spacing();
                    }

                    if (autoAcquisition2D.state == AUTO_ACQ_RUNNING) {

                        float progress = (float)autoAcquisition2D.currentShotIdx / autoAcquisition2D.totalShots;
                        char  acqBuf[64];
                        sprintf_s(acqBuf, "Acquiring: Shot %d / %d", autoAcquisition2D.currentShotIdx + 1, autoAcquisition2D.totalShots);
                        ImGui::ProgressBar(progress, ImVec2(-1, 30 * scale), acqBuf);

                        if (AppUi::Widgets::Button("ABORT AUTO GATHER", ImVec2(-1, 30 * scale))) {
                            autoAcquisition2D.state = AUTO_ACQ_IDLE;
                            state.running = false;
                            acquisition2D.isRecording = false;
                        }
                    }
                    else {
                        bool ready_to_acquire = !acquisition2D.receivers.empty() && (acquisition2D.receivers.size() == acquisition2D.numChannels);
                        if (!ready_to_acquire) ImGui::BeginDisabled();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiAccent2D.x * 0.15f, uiAccent2D.y * 0.45f, uiAccent2D.z * 0.55f, 0.75f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiAccent2D.x * 0.2f, uiAccent2D.y * 0.65f, uiAccent2D.z * 0.75f, 0.9f));
                        if (AppUi::Widgets::Button("RUN AUTO SHOT GATHER", ImVec2(-1, 40 * scale))) {
                            acquisitionAction2D([&]{
                            autoAcquisition2D.start();
                            beginExportSession2D();
                            acquisition2D.initBuffer();

                            triggerSingleShot(gl, state);
                            acquisition2D.isRecording = true;
                            state.running = true;
                            });
                        }
                        ImGui::PopStyleColor(2);

                        if (!ready_to_acquire) {
                            ImGui::EndDisabled();
                        }
                    }
                    }
                                        ImGui::EndTabItem();
                }

                if (AppUi::Widgets::BeginTabItem("Replay")) {
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Display);
                    ImGui::Spacing();

                    if(parameterGroup2D("Recording")) {


                    ImGui::Columns(2, "rec_layout", false);
                    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.65f);

                    AppUi::Widgets::SliderInt("Max Frames", &replay2D.maxFrames, 100, 1000);

                    if (replay2D.state == REPLAY_IDLE) {
                        AppUi::Widgets::SliderInt("Rec Stride", &replay2D.recordStride, 1, 20, "Every %d frames");
                    }
                    else {
                        ImGui::BeginDisabled();
                        AppUi::Widgets::SliderInt("Rec Stride", &replay2D.recordStride, 1, 20, "Every %d frames");
                        ImGui::EndDisabled();
                    }
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Save 1 frame every N simulation steps.");
                    }

                    size_t free_mem = 0, total_mem = 0;
                    float used_vram_mb = 0.0f, total_vram_mb = 0.0f;
                    if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
                        used_vram_mb = static_cast<float>(total_mem - free_mem) / (1024.0f * 1024.0f);
                        total_vram_mb = static_cast<float>(total_mem) / (1024.0f * 1024.0f);
                    }
                    float estimatedSec = (float)(replay2D.maxFrames * replay2D.recordStride) * simulationContext2D.dt;

                    AppUi::Widgets::TextColored(uiAccent2D, "GPU VRAM: %.1f / %.1f MB  |  Est. Duration: ~%.2f s", used_vram_mb, total_vram_mb, estimatedSec);

                    ImGui::NextColumn();
                    float btnH = 55.0f * scale;

                    if (replay2D.state == REPLAY_RECORDING) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));

                        if (AppUi::Widgets::Button("STOP\nREC", ImVec2(-1, btnH))) {
                            replay2D.state = REPLAY_IDLE;
                        }
                        ImGui::PopStyleColor(3);

                        float time = (float)ImGui::GetTime();
                        AppUi::Widgets::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 0.5f + 0.5f * sinf(time * 10.0f)), "REC: %d f", replay2D.frames.size());
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.6f, 0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.4f, 0.2f, 1.0f));

                        if (AppUi::Widgets::Button("START\nREC", ImVec2(-1, btnH))) {
                            replay2D.clear();
                            replay2D.state = REPLAY_RECORDING;
                            state.running = true;
                        }
                        ImGui::PopStyleColor(3);

                        if (replay2D.frames.empty()) AppUi::Widgets::TextDisabled("Buffer Empty");
                        else                         AppUi::Widgets::Text("%d Frames", replay2D.frames.size());
                    }

                    ImGui::Columns(1);
                    ImGui::Spacing();

                    }
                    if(parameterGroup2D("Playback Control")) {


                    bool hasFrames = !replay2D.frames.empty();
                    if (!hasFrames) ImGui::BeginDisabled();

                    ImGui::SetNextItemWidth(150 * scale);
                    AppUi::Widgets::SliderFloat("Speed", &replay2D.playbackSpeed, 0.1f, 5.0f, "%.1fx", ImGuiSliderFlags_Logarithmic);
                    ImGui::SameLine();

                    float ctrlBtnW = 70.0f * scale;

                    if (replay2D.state == REPLAY_PLAYING) {
                        if (AppUi::Widgets::Button("PAUSE", ImVec2(ctrlBtnW, 0))) {
                            replay2D.state = REPLAY_PAUSED;
                        }
                    }
                    else {
                        if (AppUi::Widgets::Button("PLAY", ImVec2(ctrlBtnW, 0))) {
                            replay2D.state = REPLAY_PLAYING;
                            state.running = false;
                        }
                    }

                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
                    if (AppUi::Widgets::Button("CLEAR", ImVec2(ctrlBtnW, 0))) {
                        replay2D.clear();
                    }
                    ImGui::PopStyleColor(2);

                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                    if (AppUi::Widgets::Button("SAVE", ImVec2(ctrlBtnW, 0))) {
                        char savePath[512] = { 0 };
                        if (saveSystemFileDialog(savePath, sizeof(savePath))) {
                            std::string path_str(savePath);
                            size_t dot_pos = path_str.find_last_of('.');
                            if (dot_pos != std::string::npos) {
                                path_str = path_str.substr(0, dot_pos);
                            }

                            std::string final_path = path_str + "_" + std::to_string(simulationContext2D.NX) + "x" + std::to_string(simulationContext2D.NZ) + "_vz.bin";

                            if (replay2D.exportToBinary(final_path, 1)) {
                                popupMessage2D = "Wavefield movie saved with dimensions!\nFile: " + final_path;
                                showSuccessPopup2D = true;
                            }
                            else {
                                popupMessage2D = "Failed to save wavefield movie.";
                                showErrorPopup2D = true;
                            }
                        }
                    }
                    ImGui::PopStyleColor(2);

                    if (hasFrames) {
                        char overlay[64];
                        sprintf(overlay, "%d / %d f", replay2D.currentFrameIndex + 1, (int)replay2D.frames.size());

                        int frame = replay2D.currentFrameIndex;
                        if (AppUi::Widgets::SliderInt("Timeline", &frame, 0, (int)replay2D.frames.size() - 1, overlay)) {
                            replay2D.currentFrameIndex = frame;
                            replay2D.currentFrameFloat = (float)frame;
                            replay2D.state = REPLAY_PAUSED;
                            state.running = false;
                        }
                    }
                    else {
                        ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "No Active Recording");
                    }

                    if (!hasFrames) ImGui::EndDisabled();

                    ImGui::Spacing();

                    }
                    if(parameterGroup2D("Import Wavefield Movie")) {


                    static char movie_load_path[512] = "";
                    static int  movie_load_w = 0;
                    static int  movie_load_z = 0;

                    AppUi::Widgets::InputText("Movie Path", movie_load_path, IM_ARRAYSIZE(movie_load_path));
                    ImGui::SameLine();
                    if (AppUi::Widgets::Button("Browse##Mv", ImVec2(65 * scale, 0))) {
                        char openPath[512] = { 0 };
                        if (openSystemFileDialog(openPath, sizeof(openPath),false)) {
                            strcpy_s(movie_load_path, openPath);

                            int parsed_w = 0, parsed_z = 0;
                            if (parseDimensionsFromPath(std::string(movie_load_path), parsed_w, parsed_z)) {
                                movie_load_w = parsed_w;
                                movie_load_z = parsed_z;
                            }
                            else {

                                movie_load_w = simulationContext2D.NX;
                                movie_load_z = simulationContext2D.NZ;
                            }
                        }
                    }

                    ImGui::PushItemWidth(120 * scale);
                    AppUi::Widgets::InputInt("Import NX", &movie_load_w, 0, 0); ImGui::SameLine();
                    AppUi::Widgets::InputInt("Import NZ", &movie_load_z, 0, 0);
                    ImGui::PopItemWidth();

                    bool can_load_movie = (strlen(movie_load_path) > 0) && (movie_load_w > 0) && (movie_load_z > 0);
                    if (!can_load_movie) ImGui::BeginDisabled();

                    ImGui::Spacing();
                    if (AppUi::Widgets::Button("CONFIRM LOAD MOVIE", ImVec2(-1, 30 * scale))) {
                        if (replay2D.importFromBinary(movie_load_path, movie_load_w, movie_load_z)) {
                            popupMessage2D = "Wavefield movie imported and locked successfully!\nClick [PLAY] to watch.";
                            showSuccessPopup2D = true;
                            state.running = false;

                            if (movie_load_w != simulationContext2D.NX || movie_load_z != simulationContext2D.NZ) {
                                std::cout << "[Replay IO] Triggering Auto-Reallocation for imported movie: "
                                    << movie_load_w << "x" << movie_load_z << std::endl;

                                gridWidth2D = movie_load_w;
                                gridHeight2D = movie_load_z;
                                parameters2D.model.xnum = gridWidth2D;
                                parameters2D.model.znum = gridHeight2D;

                                setupTestContext(simulationContext2D, parameters2D);
                                releaseGpuSimulation(gpuData2D);

                                if(gl.cudaSeisRes) cudaGraphicsUnregisterResource(gl.cudaSeisRes);gl.cudaSeisRes=nullptr;
                                glBindTexture(GL_TEXTURE_2D, gl.seisTex);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, simulationContext2D.NX+2*domainPad2D(), simulationContext2D.NZ+2*domainPad2D(), 0, GL_RGBA, GL_FLOAT, nullptr);


                                initializeExternalSimulation2D(gpuData2D, simulationContext2D);
                                firstAlignNeeded2D = true;
                            }
                        }
                        else {
                            popupMessage2D = "Failed to read binary stream.\nEnsure the parameters match the original export.";
                            showErrorPopup2D = true;
                        }
                    }
                    if (!can_load_movie) ImGui::EndDisabled();

                    }
                                        ImGui::EndTabItem();
                }

                if (AppUi::Widgets::BeginTabItem("Visual")) {
                    AppUi::FunctionStyleScope panelColor(AppUi::FunctionRole::Display);
                    ImGui::Spacing();
if(parameterGroup2D("Wavefield appearance")) {
                    AppUi::Widgets::Checkbox("Automatic wave amplitude",&automaticWaveScale2D);
                    AppUi::Widgets::SliderFloat("Color Gain", &colorScale2D, 0.0f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

                    const char* components[] = {
                        "Velocity Mag",
                        "Velocity X",
                        "Velocity Y",
                        "Stress Normal",
                        "Stress Shear",
                        "P-Wave (Div)",
                        "S-Wave (Curl)"
                    };
                    AppUi::Widgets::Combo("Show Component", &showComponent2D, components, IM_ARRAYSIZE(components));
                    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("W: next component | Shift+W: previous. Display gain is retained.");

                    ImGui::Spacing();

                    const char* style_types[] = {
                        "0_Seismic Bipolar",
                        "1_Coolwarm",
                        "2_PuOr",
                        "3_Grayscale",
                        "4_Inferno",
                        "5_Plasma",
                        "6_Viridis",
                        "7_Turbo",
                        "8_Magma Glow",
                        "9_Jet Rainbow",
                        "10_Hot",
                        "11_Cold",
                        "12_Terrain Map"
                    };
                    AppUi::Widgets::Combo("Visual Style", &waveStyle2D, Seismic::wavePaletteNames);
                    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("Q: next wave palette | Shift+Q: previous.");
                    ImGui::Spacing();

                    }
                    if(parameterGroup2D("Material appearance")) {
                    const char* model_styles[] = {
                        "Dark Titanium",
                        "Geological Map",
                        "Grayscale Velocity",
                        "WaveStyle Synced",
                        "Scientific White"
                    };
                    AppUi::Widgets::Combo("Geological Background", &modelStyle2D, model_styles, IM_ARRAYSIZE(model_styles));
                    if(ImGui::IsItemHovered()) AppUi::Widgets::SetTooltip("E: next model palette | Shift+E: previous.");
                    ImGui::Spacing();

                    }
                    if(parameterGroup2D("Grid and boundary display")) {
                    AppUi::Widgets::Checkbox("Show model mesh",&showModelMesh2D);
                    AppUi::Widgets::SliderFloat("Model mesh opacity",&meshOpacity2D,0,.5f,"%.2f");
                    AppUi::Widgets::Checkbox("Show wavefield",&showWave2D);
                    AppUi::Widgets::Checkbox("Show material background",&showMaterial2D);
                    AppUi::Widgets::SliderFloat("Wave opacity",&waveOpacity2D,0,1,"%.2f");
                    if(AppUi::Widgets::Checkbox("Show external absorbing boundary (B)",&showBoundary2D)) resetViewportRequested2D=true;
                    AppUi::Widgets::Checkbox("Show Background Grid", &showGrid2D);
                    if (ImGui::IsItemHovered()) {
                        AppUi::Widgets::SetTooltip("Show major and minor grid lines inside the physical domain. Boundary lines remain visible.");
                    }

                    AppUi::Widgets::SliderFloat("Grid Spacing (m)", &uiGridSpacing2D, 10.0f, 200.0f);
                    AppUi::Widgets::SliderFloat("Grid Opacity", &uiGridOpacity2D, 0.0f, 1.0f);

                    }
                                        ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Spacing();ImGui::Separator();
            float avail_w = ImGui::GetContentRegionAvail().x;
            float reset_btn_w = 60.0f * scale;
            float picker_w = avail_w - reset_btn_w - ImGui::GetStyle().ItemSpacing.x;

            ImGui::PushItemWidth(picker_w);
            AppUi::Widgets::ColorEdit3("##AccentColorPicker", (float*)&uiAccent2D, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueBar);
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered()) {
                AppUi::Widgets::SetTooltip("Interface Hue Accent Color");
            }

            ImGui::SameLine();
            if (AppUi::Widgets::Button("Reset##Theme", ImVec2(reset_btn_w, 0))) {
                uiAccent2D = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            }
            if (ImGui::IsItemHovered()) {
                AppUi::Widgets::SetTooltip("Reset UI Accent Color to Default Cyan");
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(8);
    }

    if (showMonitorPar2D) {
        ImGui::SetNextWindowPos(ImVec2(30 * scale, 80 * scale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420 * scale, 520 * scale), ImGuiCond_FirstUseEver);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.08f, 0.1f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Border, uiAccent2D);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

        ImGui::PushStyleColor(ImGuiCol_HeaderActive, AppUi::palette().raised);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(uiAccent2D.x * 0.8f, uiAccent2D.y * 0.8f, uiAccent2D.z * 0.8f, 0.8f));

        if (AppUi::Widgets::Begin("Simulation & PML Monitor", &showMonitorPar2D)) {
            static float displayFPS = 60.0f;
            displayFPS = displayFPS * 0.95f + io.Framerate * 0.05f;
            AppUi::Widgets::TextColored(uiAccent2D, "MONITOR STATE  |  FPS: %.1f", displayFPS);
            ImGui::Separator();

            ImGui::Spacing();
            if (AppUi::Widgets::CollapsingHeader("ACTIVE PARAMETERS MONITOR", ImGuiTreeNodeFlags_DefaultOpen)) {

                float time = (float)ImGui::GetTime();
                float alpha = 0.35f + 0.65f * (sinf(time * 6.0f) * 0.5f + 0.5f);
                ImVec4 warningCol = ImVec4(1.0f, 0.82f, 0.0f, alpha);

                AppUi::Widgets::TextDisabled("Grid Dimensions:"); ImGui::SameLine();
                AppUi::Widgets::Text("%d x %d (Total: %d)", simulationContext2D.NX, simulationContext2D.NZ, simulationContext2D.total_grid);

                AppUi::Widgets::TextDisabled("Physical Scale:"); ImGui::SameLine();
                AppUi::Widgets::Text("%.1f m x %.1f m", simulationContext2D.NX * simulationContext2D.h, simulationContext2D.NZ * simulationContext2D.h);

                size_t free_mem = 0;
                size_t total_mem = 0;
                float used_vram_mb = 0.0f;
                float total_vram_mb = 0.0f;

                if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
                    used_vram_mb = static_cast<float>(total_mem - free_mem) / (1024.0f * 1024.0f);
                    total_vram_mb = static_cast<float>(total_mem) / (1024.0f * 1024.0f);
                }

                AppUi::Widgets::TextDisabled("System GPU VRAM:"); ImGui::SameLine();
                AppUi::Widgets::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "%.1f / %.1f MB", used_vram_mb, total_vram_mb);
                if (ImGui::IsItemHovered()) {
                    AppUi::Widgets::SetTooltip("System-wide allocated VRAM on the active GPU (Current Used / Total Capacity).");
                }

                AppUi::Widgets::TextDisabled("Grid VRAM (Est.):"); ImGui::SameLine();
                AppUi::Widgets::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "%.2f MB", cachedVramMb2D);
                if (ImGui::IsItemHovered()) {
                    AppUi::Widgets::SetTooltip("Estimated memory allocated strictly for this finite-difference simulation grid.");
                }

                deviceMonitor2D.load();

                unsigned int gpu_util = 0;
                unsigned int gpu_temp = 0;
                unsigned int gpu_power_mw = 0;
                bool         nvml_ok = false;

                if (deviceMonitor2D.loaded) {
                    nvmlUtilization_t util{};
                    if (deviceMonitor2D.getUtil(deviceMonitor2D.devHandle, &util) == NVML_SUCCESS) {
                        gpu_util = util.gpu;
                    }
                    if (deviceMonitor2D.getTemp(deviceMonitor2D.devHandle, NVML_TEMPERATURE_GPU, &gpu_temp) == NVML_SUCCESS) {

                    }
                    if (deviceMonitor2D.getPower(deviceMonitor2D.devHandle, &gpu_power_mw) == NVML_SUCCESS) {

                    }
                    nvml_ok = true;
                }

                AppUi::Widgets::TextDisabled("GPU Core Usage:"); ImGui::SameLine();
                if (nvml_ok) {
                    AppUi::Widgets::TextColored(uiAccent2D, "%u %%", gpu_util);
                }
                else {
                    AppUi::Widgets::TextDisabled("N/A (NVML Offline)");
                }
                if (ImGui::IsItemHovered()) {
                    AppUi::Widgets::SetTooltip("Active SM core utilization rate (same as Task Manager's CUDA chart).");
                }

                AppUi::Widgets::TextDisabled("GPU Temperature:"); ImGui::SameLine();
                if (nvml_ok) {

                    ImVec4 tempColor = (gpu_temp >= 82) ? ImVec4(1.0f, 0.2f, 0.2f, 1.0f) : uiAccent2D;
                    AppUi::Widgets::TextColored(tempColor, "%u oC", gpu_temp);
                }
                else {
                    AppUi::Widgets::TextDisabled("N/A (NVML Offline)");
                }
                if (ImGui::IsItemHovered()) {
                    AppUi::Widgets::SetTooltip("Real-time GPU core temperature.");
                }

                AppUi::Widgets::TextDisabled("GPU Power Draw:"); ImGui::SameLine();
                if (nvml_ok) {
                    AppUi::Widgets::TextColored(uiAccent2D, "%.1f W", static_cast<float>(gpu_power_mw) / 1000.0f);
                }
                else {
                    AppUi::Widgets::TextDisabled("N/A (NVML Offline)");
                }
                if (ImGui::IsItemHovered()) {
                    AppUi::Widgets::SetTooltip("Real-time GPU board power consumption (Watts).");
                }

                AppUi::Widgets::TextDisabled("Active Spacing (dx):"); ImGui::SameLine();
                AppUi::Widgets::Text("%.1f m", simulationContext2D.h);
                if (std::abs(tempDx2D - simulationContext2D.h) > 1e-4f) {
                    ImGui::SameLine();
                    AppUi::Widgets::TextColored(warningCol, "[Apply Pending]");
                }

                AppUi::Widgets::TextDisabled("Active Time Step (dt):"); ImGui::SameLine();
                AppUi::Widgets::Text("%.6f s", simulationContext2D.dt);
                if (std::abs(tempDt2D - simulationContext2D.dt) > 1e-7f) {
                    ImGui::SameLine();
                    AppUi::Widgets::TextColored(warningCol, "[Apply Pending]");
                }

                AppUi::Widgets::TextDisabled("Active Max Steps (nt):"); ImGui::SameLine();
                AppUi::Widgets::Text("%d steps (%.2f s simulated)", simulationContext2D.nt, simulationContext2D.nt * simulationContext2D.dt);
                if (tempNt2D != simulationContext2D.nt) {
                    ImGui::SameLine();
                    AppUi::Widgets::TextColored(warningCol, "[Apply Pending]");
                }

                AppUi::Widgets::TextDisabled("Active PML Width:"); ImGui::SameLine();
                AppUi::Widgets::Text("%d px", simulationContext2D.npml);
                if (tempPml2D != simulationContext2D.npml) {
                    ImGui::SameLine();
                    AppUi::Widgets::TextColored(warningCol, "[Apply Pending]");
                }

                int active_src_x = simulationContext2D.src_idx % simulationContext2D.NX;
                int active_src_z = simulationContext2D.src_idx / simulationContext2D.NX;
                AppUi::Widgets::TextDisabled("Active Source Pos:"); ImGui::SameLine();
                AppUi::Widgets::Text("(%d, %d)", active_src_x, active_src_z);
                if (editSrcX2D != active_src_x || editSrcZ2D != active_src_z) {
                    ImGui::SameLine();
                    AppUi::Widgets::TextColored(warningCol, "[Apply Pending]");
                }

                ImGui::Separator();
                AppUi::Widgets::TextColored(uiAccent2D, "ACTIVE GEOPHYSICAL METRICS");

                AppUi::Widgets::TextDisabled("P-Wave Velocity (Vp):"); ImGui::SameLine();
                AppUi::Widgets::Text("%.1f m/s ~ %.1f m/s", cachedMinVp2D, cachedMaxVp2D);

                AppUi::Widgets::TextDisabled("S-Wave Velocity (Vs):"); ImGui::SameLine();
                AppUi::Widgets::Text("%.1f m/s ~ %.1f m/s", cachedMinVs2D, cachedMaxVs2D);

                AppUi::Widgets::TextDisabled("Density Range (Rho):"); ImGui::SameLine();
                AppUi::Widgets::Text("%.1f ~ %.1f kg/m^3", cachedMinRho2D, cachedMaxRho2D);
            }

            ImGui::Spacing();
            AppUi::Widgets::TextDisabled("PML Damping Profile (dx) Real-Time Monitor:");

            std::vector<float> pml_x_axis(gpuData2D.NX),pml_damping(gpuData2D.NX);
            for(int j=0;j<gpuData2D.NX;++j) pml_x_axis[j]=float(j-gpuData2D.originX);
            CUDA_CHECK(solverCopy2D(gpuData2D,pml_damping.data(),gpuData2D.d_dx,pml_damping.size()*sizeof(float),cudaMemcpyDeviceToHost));

            ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(10, 12, 15, 255));
            ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(0, 0, 0, 255));
            ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(0, 255, 120, 255));
            ImPlot::PushStyleColor(ImPlotCol_AxisGrid, IM_COL32(35, 35, 35, 120));

            if (ImPlot::BeginPlot("##PmlProfilePlot", ImVec2(-1, 140 * scale), ImPlotFlags_NoLegend)) {
                ImPlot::SetupAxes(AppUi::text("Grid Node Index"), AppUi::text("Damping (Hz)"), ImPlotAxisFlags_NoTickLabels, 0);

                float max_d = 1.0f;
                for (float val : pml_damping) {
                    if (val > max_d) max_d = val;
                }
                ImPlot::SetupAxesLimits(-float(domainPad2D()),float(simulationContext2D.NX-1+domainPad2D()), -max_d * 0.05f, max_d * 1.1f);

                ImPlot::PlotLine("Damping dx", pml_x_axis.data(),pml_damping.data(),gpuData2D.NX);

                double pml_left_boundary = 0;
                double pml_right_boundary = simulationContext2D.NX-1;

                ImVec2 left_p1 = ImPlot::PlotToPixels(ImPlotPoint(pml_left_boundary, -max_d * 0.05f));
                ImVec2 left_p2 = ImPlot::PlotToPixels(ImPlotPoint(pml_left_boundary, max_d * 1.1f));
                ImVec2 right_p1 = ImPlot::PlotToPixels(ImPlotPoint(pml_right_boundary, -max_d * 0.05f));
                ImVec2 right_p2 = ImPlot::PlotToPixels(ImPlotPoint(pml_right_boundary, max_d * 1.1f));

                ImPlot::GetPlotDrawList()->AddLine(left_p1, left_p2, IM_COL32(255, 140, 0, 180), 1.5f);
                ImPlot::GetPlotDrawList()->AddLine(right_p1, right_p2, IM_COL32(255, 140, 0, 180), 1.5f);

                ImPlot::EndPlot();
            }
            ImPlot::PopStyleColor(4);
            ImGui::Spacing();
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }

    renderRecordViewer2D(recordViewer2D);

    {
        ImGui::SetNextWindowPos({ 0, (float)winH - barHeight });
        ImGui::SetNextWindowSize({ (float)winW, barHeight });

        ImVec4 botBarBg = AppUi::palette().background;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, botBarBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * scale, 0));

        AppUi::Widgets::Begin("BottomBar_Seis", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::SetCursorPosY(6.0f * scale);
            if (ImGui::BeginTable("Status2D", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Coordinates", ImGuiTableColumnFlags_WidthStretch, .48f);
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch, .52f);
                ImGui::TableNextColumn();
                if (hoverValid2D)
                    AppUi::Widgets::Text("Grid [%d, %d] | Position [%.1f, %.1f] m", hoverMx2D, hoverMy2D,
                        hoverMx2D * simulationContext2D.h, hoverMy2D * simulationContext2D.h);
                else
                    AppUi::Widgets::TextDisabled("Hover over the wavefield to inspect coordinates");
                ImGui::TableNextColumn();
                if (hoverValid2D)
                    AppUi::Widgets::Text("Vp %.0f | Vs %.0f m/s | Density %.0f kg/m^3", hoverVp2D, hoverVs2D, hoverRho2D);
                else
                    AppUi::Widgets::TextDisabled("Acquired samples: %d | Receivers: %d", acquisition2D.capturedSamples, acquisition2D.numChannels);
                ImGui::EndTable();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(1);

        ImDrawList* botDraw = ImGui::GetBackgroundDrawList();
        botDraw->AddLine(
            { 0, (float)winH - barHeight },
            { (float)winW, (float)winH - barHeight },
            IM_COL32(uiAccent2D.x * 255, uiAccent2D.y * 255, uiAccent2D.z * 255, 60),
            1.0f
        );
    }
}

/** Runs one frame of the 2D seismic workspace: initialize, interact, compute, render, and present HUD. */
inline void renderSeisSimScreenGpu(SimState& state, int winW, int winH, GLHandles& gl, const GpuInfo& info) {

    const float scale = (float)winW / 1920.0f;
    const float barHeight = 38.0f * scale;

    initializeSeismicSimulation(gl);

    prepareDockspace2D(winW,winH,barHeight);
    auto windowClass=dockWindowClass2D();ImGui::SetNextWindowClass(&windowClass);
    renderCurrentParameters2D(state);
    const bool canvasVisible=AppUi::Widgets::Begin(canvasName2D,nullptr,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    canvasHovered2D=false;
    if(canvasVisible) {
        if(AppUi::Widgets::SmallButton(state.running?"Pause":"Run")) state.running=!state.running;
        ImGui::SameLine();if(AppUi::Widgets::SmallButton("Reset View")) resetViewportRequested2D=true;
        ImGui::SameLine();if(AppUi::Widgets::SmallButton("Boundary (B)")) {showBoundary2D=!showBoundary2D;resetViewportRequested2D=true;}
        ImGui::SameLine();ImGui::BeginDisabled(acquisition2D.isRecording);
        AppUi::Widgets::Checkbox("Continuous fire",&multiSourceMode2D);ImGui::EndDisabled();
        AppUi::Widgets::TextDisabled(multiSourceMode2D?"Left: fire / hold to repeat | Middle or right drag: pan | Wheel: zoom":"Left: fire once | Middle or right drag: pan | Wheel: zoom");
        AppUi::Widgets::Text("Physical model: %d x %d | %.2f x %.2f m | f0 %.1f Hz",simulationContext2D.NX,simulationContext2D.NZ,
            (simulationContext2D.NX-1)*simulationContext2D.h,(simulationContext2D.NZ-1)*simulationContext2D.h,appliedSourceFrequency2D);
        canvasOrigin2D=ImGui::GetCursorScreenPos();canvasSize2D=ImGui::GetContentRegionAvail();
        const float rulerGutter=AppUi::Widgets::CalcTextSize("-9999m").x+14;
        const float axisGutter=ImGui::GetTextLineHeight()+14;
        canvasOrigin2D.x+=rulerGutter;
        canvasSize2D.x-=rulerGutter;
        canvasSize2D.y-=2*(ImGui::GetTextLineHeight()+ImGui::GetStyle().ItemSpacing.y+10)+axisGutter;
        canvasSize2D.x=std::max(16.0f,canvasSize2D.x);canvasSize2D.y=std::max(16.0f,canvasSize2D.y);
        const auto mouse=ImGui::GetMousePos();
        canvasHovered2D=ImGui::IsWindowHovered()&&mouse.x>=canvasOrigin2D.x&&mouse.y>=canvasOrigin2D.y&&
            mouse.x<canvasOrigin2D.x+canvasSize2D.x&&mouse.y<canvasOrigin2D.y+canvasSize2D.y;
    }
    const int canvasWidth=std::max(16,int(canvasSize2D.x)),canvasHeight=std::max(16,int(canvasSize2D.y));
    applyCameraAutoAlignment(canvasWidth,canvasHeight,0);
    handleSeismicInteractions(state,canvasWidth,canvasHeight,0);

    if (replay2D.state != REPLAY_PLAYING && replay2D.state != REPLAY_PAUSED) {

        updateWavefieldSimulation(state);

        updateAutoAcquisitionWorkflow(state, gl);
    }
    else {

        ImGuiIO& io = ImGui::GetIO();

        if (replay2D.state == REPLAY_PLAYING && !replay2D.frames.empty()) {

            replay2D.currentFrameFloat += replay2D.playbackSpeed * (io.DeltaTime * 60.0f);
            replay2D.currentFrameIndex = static_cast<int>(replay2D.currentFrameFloat);

            if (replay2D.currentFrameIndex >= static_cast<int>(replay2D.frames.size())) {
                replay2D.currentFrameIndex = 0;
                replay2D.currentFrameFloat = 0.0f;
            }
            replay2D.uploadFrameToGpu(replay2D.currentFrameIndex, gpuData2D);
        }
        else if (replay2D.state == REPLAY_PAUSED && !replay2D.frames.empty()) {

            replay2D.uploadFrameToGpu(replay2D.currentFrameIndex, gpuData2D);
        }
    }

    if(canvasVisible) {
        struct TargetGuard2D {
            GLint draw=0,read=0,viewport[4]{};
            GLboolean blend=GL_FALSE,scissor=GL_FALSE;
            TargetGuard2D(){glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_VIEWPORT,viewport);
                blend=glIsEnabled(GL_BLEND);scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);}
            ~TargetGuard2D(){glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
                if(blend) glEnable(GL_BLEND);else glDisable(GL_BLEND);if(scissor) glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);}
        };
        {
            TargetGuard2D guard;
            const auto scalePixels=ImGui::GetIO().DisplayFramebufferScale;
            ensureCanvasTarget2D(int(canvasSize2D.x*scalePixels.x),int(canvasSize2D.y*scalePixels.y));
            glBindFramebuffer(GL_FRAMEBUFFER,canvasGl2D.framebuffer);
            const auto background=AppUi::palette().background;
            glClearColor(background.x,background.y,background.z,1);glClear(GL_COLOR_BUFFER_BIT);
            renderFullBackbufferWavefield(canvasGl2D.width,canvasGl2D.height,0,gl);
        }
        ImGui::SetCursorScreenPos(canvasOrigin2D);
        ImGui::Image(ImTextureID(canvasGl2D.texture),canvasSize2D,ImVec2(0,1),ImVec2(1,0));
        canvasDrawList2D=ImGui::GetWindowDrawList();
        renderGridRulerOnBackbuffer(simulationContext2D,state,canvasWidth,canvasHeight,0,viewOffset2D,viewZoom2D);
        canvasDrawList2D->PushClipRect(canvasOrigin2D,ImVec2(canvasOrigin2D.x+canvasSize2D.x,canvasOrigin2D.y+canvasSize2D.y),true);
        drawCanvasAnnotations2D(state,canvasWidth,canvasHeight);
        canvasDrawList2D->PopClipRect();canvasDrawList2D=nullptr;
        ImGui::SetCursorScreenPos(ImVec2(canvasOrigin2D.x,canvasOrigin2D.y+canvasSize2D.y+ImGui::GetTextLineHeight()+14));
        drawColorLegends2D();
    }
    ImGui::End();

    renderSeisHud(state, winW, winH, barHeight, scale, gl, info);

    renderSeisPopups();
}
