// One application path shared by the control tab and current/draft inspector.
inline void applyPendingParameters2D(SimState& state,bool includeGrid) {
    if(acquisition2D.isRecording) return;
    if(!std::isfinite(tempDt2D)||!std::isfinite(tempDx2D)||tempDt2D<=0||tempDx2D<=0||tempNt2D<1||tempPml2D<0)
        throw std::invalid_argument("Invalid draft time/grid settings");
    if(tempNt2D>50000||tempPml2D>100||tempDx2D>1000||sourceFrequency2D<=0||!std::isfinite(sourceFrequency2D)) throw std::invalid_argument("Draft settings exceed supported bounds");
    if(includeGrid&&(gridWidth2D!=simulationContext2D.NX||gridHeight2D!=simulationContext2D.NZ)) {
        gridWidth2D=std::clamp(gridWidth2D,64,8192);gridHeight2D=std::clamp(gridHeight2D,64,8192);
        parameters2D.model.xnum=gridWidth2D;parameters2D.model.znum=gridHeight2D;
        setupTestContext(simulationContext2D,parameters2D);
        editSrcX2D=std::min(editSrcX2D,gridWidth2D-1);editSrcZ2D=std::min(editSrcZ2D,gridHeight2D-1);
        firstAlignNeeded2D=true;
    }
    float max_vp = 0.0f;
    for (int k = 0; k < simulationContext2D.total_grid; ++k) {
        float vp = sqrtf(simulationContext2D.lambda2mu[k] / simulationContext2D.rho[k]);
        if (vp > max_vp) max_vp = vp;
    }
    if (max_vp <= 350.0f) max_vp = 2000.0f;

    float dt_theory_limit = kCourantLimit2d * tempDx2D / max_vp;

    if (tempDt2D > dt_theory_limit) {
        tempDt2D = kSafeCourant2d * tempDx2D / max_vp;
        popupMessage2D = "Warning: Input dt exceeded CFL stability limit.\nAutomatically clamped to safe value.";
        showErrorPopup2D = true;
    }

    simulationContext2D.dt = tempDt2D;
    simulationContext2D.nt = tempNt2D;
    simulationContext2D.h = tempDx2D;

    parameters2D.model.dx = tempDx2D;
    parameters2D.model.dz = tempDx2D;
    simulationContext2D.npml = tempPml2D;
    parameters2D.FDM.npml = (float)tempPml2D;

    simulationContext2D.c1_h = static_cast<float>(kStaggeredDerivativeWeights[0]) / tempDx2D;
    simulationContext2D.c2_h = static_cast<float>(kStaggeredDerivativeWeights[1]) / tempDx2D;
    simulationContext2D.c3_h = static_cast<float>(kStaggeredDerivativeWeights[2]) / tempDx2D;
    simulationContext2D.c4_h = static_cast<float>(kStaggeredDerivativeWeights[3]) / tempDx2D;

    updatePmlDampingArrays();

    simulationContext2D.dp_flat.assign(simulationContext2D.NX, 0.0f);
    int fs_idx = simulationContext2D.npml;
    for (int j = 0; j < simulationContext2D.NX; ++j) {
        int k = fs_idx * simulationContext2D.NX + j;
        if (k < simulationContext2D.total_grid && simulationContext2D.lambda2mu[k] > 0.0f) {
            float l2m = simulationContext2D.lambda2mu[k];
            float lam = simulationContext2D.lambda[k];
            simulationContext2D.dp_flat[j] = (l2m * l2m - lam * lam) / l2m;
        }
    }

    simulationContext2D.record_vx.assign(static_cast<size_t>(simulationContext2D.num_rcv) * simulationContext2D.nt, 0.0f);
    simulationContext2D.record_vz.assign(static_cast<size_t>(simulationContext2D.num_rcv) * simulationContext2D.nt, 0.0f);
    parameters2D.FDM.f0=sourceFrequency2D;parameters2D.FDM.t0=sourceDelay2D;
    appliedSourceFrequency2D=sourceFrequency2D;appliedSourceDelay2D=sourceDelay2D;
    generateRickerWavelet(simulationContext2D.wavelet, simulationContext2D.nt, simulationContext2D.dt, parameters2D.FDM.f0, parameters2D.FDM.t0);

    resetSimRequested2D = true;

    parameters2D.FDM.dt=simulationContext2D.dt;parameters2D.FDM.nt=simulationContext2D.nt;
    state.running=false;resetSimRequested2D=true;
}
inline bool pendingParameters2D() {
    return gridWidth2D!=simulationContext2D.NX||gridHeight2D!=simulationContext2D.NZ||
        tempDt2D!=simulationContext2D.dt||tempDx2D!=simulationContext2D.h||tempNt2D!=simulationContext2D.nt||tempPml2D!=simulationContext2D.npml||
        (selectedBackend2D!=0&&gpuData2D.cpu!=(selectedBackend2D==1))||workerCount2D!=gpuData2D.workers||sourceFrequency2D!=appliedSourceFrequency2D||sourceDelay2D!=appliedSourceDelay2D;
}
inline void renderCurrentParameters2D(SimState& state) {
    auto windowClass=dockWindowClass2D();ImGui::SetNextWindowClass(&windowClass);
    if(AppUi::Widgets::Begin("Current Parameters###Seis2D.Parameters")) {
        AppUi::Widgets::TextColored(AppUi::functionColor(pendingParameters2D()?AppUi::FunctionRole::Warning:AppUi::FunctionRole::Simulation),
            pendingParameters2D()?"Unapplied changes":"Parameters are up to date");
        drawParameterGroupTools2D();
        if(parameterGroup2D("Active / draft parameters")) {
        if(ImGui::BeginTable("ActiveDraft",3,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(AppUi::text("Parameter"));ImGui::TableSetupColumn(AppUi::text("Current"));ImGui::TableSetupColumn(AppUi::text("Draft"));ImGui::TableHeadersRow();
            auto row=[](const char* label,double active,double draft,const char* format) {
                ImGui::TableNextRow();ImGui::TableNextColumn();AppUi::Widgets::TextUnformatted(label);
                ImGui::TableNextColumn();ImGui::Text(format,active);ImGui::TableNextColumn();
                if(active!=draft) ImGui::PushStyleColor(ImGuiCol_Text,AppUi::functionColor(AppUi::FunctionRole::Warning));
                ImGui::Text(format,draft);if(active!=draft) ImGui::PopStyleColor();
            };
            row("NX",simulationContext2D.NX,gridWidth2D,"%.0f");row("NZ",simulationContext2D.NZ,gridHeight2D,"%.0f");
            row("dt (ms)",simulationContext2D.dt*1000,tempDt2D*1000,"%.4f");
            row("dx (m)",simulationContext2D.h,tempDx2D,"%.3f");row("PML",simulationContext2D.npml,tempPml2D,"%.0f");
            row("nt",simulationContext2D.nt,tempNt2D,"%.0f");row("CPU threads",gpuData2D.workers,workerCount2D,"%.0f");
            row("f0 (Hz)",appliedSourceFrequency2D,sourceFrequency2D,"%.2f");
            ImGui::EndTable();
        }
        AppUi::Widgets::Text("%s: %s",AppUi::text("Current"),gpuData2D.cpu?"CPU / OpenMP":"CUDA");
        AppUi::Widgets::Text("%s: %s",AppUi::text("Draft"),selectedBackend2D==0?"Auto":selectedBackend2D==1?"CPU / OpenMP":"CUDA");
        AppUi::Widgets::Text("CFL %.4f / %.4f",simulationContext2D.dt*cachedMaxVp2D/simulationContext2D.h,kCourantLimit2d);
        AppUi::Widgets::Text("%d x %d | %.2f x %.2f m",simulationContext2D.NX,simulationContext2D.NZ,(simulationContext2D.NX-1)*simulationContext2D.h,(simulationContext2D.NZ-1)*simulationContext2D.h);
        AppUi::Widgets::Text("%s: %.3f ms",AppUi::text("Sample interval"),acquisition2D.actualSampleInterval*1000);
        }
        if(parameterGroup2D("Update simulation parameters")) {
        ImGui::BeginDisabled(acquisition2D.isRecording);
        AppUi::Widgets::InputInt("Grid Width (NX)",&gridWidth2D);AppUi::Widgets::InputInt("Grid Height (NZ)",&gridHeight2D);
        float dtms=tempDt2D*1000;if(AppUi::Widgets::InputFloat("Time step (ms)",&dtms,.001f,.01f,"%.4f")) tempDt2D=dtms*.001f;
        AppUi::Widgets::InputFloat("Grid Spacing (dx)",&tempDx2D,.1f,1,"%.3f");
        AppUi::Widgets::InputInt("Max Steps (nt)",&tempNt2D);AppUi::Widgets::InputInt("PML Layer Width",&tempPml2D);
        AppUi::Widgets::TextWrapped("Changing grid dimensions rebuilds the material model and clears the wavefield. Updating time or backend clears the wavefield and retains materials.");
        if(AppUi::Widgets::Button("Update parameters",ImVec2(-1,0))) {
            try {applyPendingParameters2D(state,true);}catch(const std::exception& e) {popupMessage2D=e.what();showErrorPopup2D=true;}
        }
        if(AppUi::Widgets::Button("Discard pending changes",ImVec2(-1,0))) {
            gridWidth2D=simulationContext2D.NX;gridHeight2D=simulationContext2D.NZ;tempDt2D=simulationContext2D.dt;tempDx2D=simulationContext2D.h;
            tempNt2D=simulationContext2D.nt;tempPml2D=simulationContext2D.npml;workerCount2D=gpuData2D.workers;sourceFrequency2D=appliedSourceFrequency2D;sourceDelay2D=appliedSourceDelay2D;selectedBackend2D=gpuData2D.cpu?1:2;
        }
        ImGui::EndDisabled();
        }
    }
    ImGui::End();
}
