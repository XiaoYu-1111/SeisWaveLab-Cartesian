// Private implementation fragment, included once by Simulation2D.cpp.
// Allocate the 2D host fields, CUDA solver and GL interop resources on first entry.
inline void initializeSeismicSimulation(GLHandles& gl) {
    if (!initialized2D) {
        editSrcX2D = gridWidth2D / 2;
        editSrcZ2D = gridHeight2D / 4;

        parameters2D.model.xnum = gridWidth2D;
        parameters2D.model.znum = gridHeight2D;
        parameters2D.model.dx = 1.0f;
        parameters2D.model.dz = 1.0f;
        parameters2D.FDM.f0 = sourceFrequency2D;
        parameters2D.FDM.t0 = sourceDelay2D;

        setupTestContext(simulationContext2D, parameters2D);
        initializeExternalSimulation2D(gpuData2D, simulationContext2D);

        glGenTextures(1, &gl.seisTex);
        glBindTexture(GL_TEXTURE_2D, gl.seisTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, simulationContext2D.NX+2*domainPad2D(), simulationContext2D.NZ+2*domainPad2D(), 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


        initialized2D = true;
        firstAlignNeeded2D = true;
        updateModelTexture();

        autoAcquisition2D.shotDepthZ = editSrcZ2D;
        autoAcquisition2D.startX = gridWidth2D / 5;
    }
}

/** Fits the 2D model aspect ratio to the available viewport after initialization or resize. */
inline void applyCameraAutoAlignment(int winW, int winH, float barHeight) {
    if (firstAlignNeeded2D && winH > 0 && simulationContext2D.NX > 0) {
        float  winAspect = (float)winW / (float)winH;
        float  simAspect = (float)viewWidth2D() / (float)viewHeight2D();
        ImVec2 aspectCorr = { 1.0f, 1.0f };

        if (winAspect > simAspect) {
            aspectCorr.x = winAspect / simAspect;
        }
        else {
            aspectCorr.y = simAspect / winAspect;
        }

        viewOffset2D=ImVec2(0,0);viewZoom2D=1;
        firstAlignNeeded2D = false;
    }
}

/** Converts viewport input into model edits, source/receiver placement, and camera controls. */
inline void handleSeismicInteractions(SimState& state, int winW, int winH, float barHeight) {
    ImGuiIO& io = ImGui::GetIO();

    if (resetSimRequested2D) {
        acquisition2D.isRecording=false;
        autoAcquisition2D.state=AUTO_ACQ_IDLE;
        currentIt2D = 0;
        state.running = false;
        elapsedRunWallTime2D = 0.0f;
        activeSources2D.clear();
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

        releaseGpuSimulation(gpuData2D);
        initializeExternalSimulation2D(gpuData2D, simulationContext2D);
        updateModelTexture();
        recalculateCachedModelMetrics();
        std::fill(simulationContext2D.record_vx.begin(), simulationContext2D.record_vx.end(), 0.0f);
        std::fill(simulationContext2D.record_vz.begin(), simulationContext2D.record_vz.end(), 0.0f);
        resetSimRequested2D = false;
    }

    if (resetViewportRequested2D) {
        viewZoom2D = 1.0f;

        float  winAspect = (float)winW / (float)winH;
        float  simAspect = (float)viewWidth2D() / (float)viewHeight2D();
        ImVec2 aspectCorr = { 1.0f, 1.0f };

        if (winAspect > simAspect) {
            aspectCorr.x = winAspect / simAspect;
        }
        else {
            aspectCorr.y = simAspect / winAspect;
        }

        viewOffset2D=ImVec2(0,0);

        resetViewportRequested2D = false;
    }

    if(canvasHovered2D&&(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)||ImGui::IsMouseClicked(ImGuiMouseButton_Right))) panActive2D=true;
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle)&&!ImGui::IsMouseDown(ImGuiMouseButton_Right)) panActive2D=false;

    // Match the 3D Q/W/E order, preserving all amplitude and model parameters.
    if (AppUi::workspaceShortcutsAvailable(true)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q,false)) waveStyle2D=AppUi::cycleDisplaySelection(waveStyle2D,14,io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_W,false)) showComponent2D=AppUi::cycleDisplaySelection(showComponent2D,7,io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_E,false)) modelStyle2D=AppUi::cycleDisplaySelection(modelStyle2D,5,io.KeyShift);
        if (!io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_B,false)) {showBoundary2D=!showBoundary2D;resetViewportRequested2D=true;}
        if (!io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_G,false)) showGrid2D=!showGrid2D;
    }

    float  winAspect = (float)winW / (float)winH;
    float  simAspect = (float)viewWidth2D() / (float)viewHeight2D();
    ImVec2 aspectCorr = { 1.0f, 1.0f };
    if (winAspect > simAspect) aspectCorr.x = winAspect / simAspect;
    else                         aspectCorr.y = simAspect / winAspect;

    double        current_time = glfwGetTime();
    bool          trigger_active = false;

    const bool canInject=canvasHovered2D&&!acquisition2D.isRecording&&state.brushType==TOOL_NONE&&!ImGui::IsMouseDown(ImGuiMouseButton_Middle)&&!ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (multiSourceMode2D) {
        if (canInject&&ImGui::IsMouseDown(0) && (current_time - lastInjectTime2D >= 0.10)) {
            trigger_active = true;
            lastInjectTime2D = current_time;
        }
    }
    else {
        if (canInject&&ImGui::IsMouseClicked(0)) {
            trigger_active = true;
        }
    }

    if(ImGui::IsMouseDown(ImGuiMouseButton_Middle)||ImGui::IsMouseDown(ImGuiMouseButton_Right)) trigger_active=false;
    hoverValid2D=false;isMouseInside2D=false;
    if (canvasHovered2D && !acquisition2D.isRecording) {
        float normX = (io.MousePos.x-canvasOrigin2D.x) / (float)winW;
        float normY = (io.MousePos.y-canvasOrigin2D.y) / (float)winH;

        float worldX = (normX - 0.5f) * (aspectCorr.x / viewZoom2D) + 0.5f + (0.5f * viewOffset2D.x);
        float worldY = (normY - 0.5f) * (aspectCorr.y / viewZoom2D) + 0.5f - (0.5f * viewOffset2D.y);

        int mx = int(std::floor(worldX * viewWidth2D()))+viewOrigin2D();
        int my = int(std::floor(worldY * viewHeight2D()))+viewOriginZ2D();
        if(freeTop2D()&&my==0&&state.brushType==TOOL_NONE) my=1;

        if (state.brushType == TOOL_NONE) {

            if (trigger_active) {
                int min_valid = 0;
                int max_valid_x = simulationContext2D.NX - 1;
                int max_valid_z = simulationContext2D.NZ - 1;

                if (mx >= min_valid && mx <= max_valid_x && my >= min_valid && my <= max_valid_z) {
                    int clicked_idx = my * simulationContext2D.NX + mx;

                    if (multiSourceMode2D) {
                        GPUSource new_src;
                        new_src.idx = clicked_idx;
                        new_src.t = 0.0f;
                        new_src.f_peak = sourceFrequency2D;
                        new_src.amp = editAmp2D;
                        simulationContext2D.src_type=editSourceType2D;simulationContext2D.src_angle=editAngle2D;

                        if (activeSources2D.size() < 150) {
                            activeSources2D.push_back(new_src);
                        }
                        editSrcX2D = mx;
                        editSrcZ2D = my;
                        state.running=true;
                        if(currentIt2D>=simulationContext2D.nt) infiniteMode2D=true;
                    }
                    else {
                        simulationContext2D.src_z_idx = my;
                        simulationContext2D.src_idx = clicked_idx;
                        editSrcX2D = mx;
                        editSrcZ2D = my;
                        currentIt2D = 0;
                        state.running = true;
                        elapsedRunWallTime2D = 0.0f;

                        releaseGpuSimulation(gpuData2D);
                        initializeExternalSimulation2D(gpuData2D, simulationContext2D);
                        std::fill(simulationContext2D.record_vx.begin(), simulationContext2D.record_vx.end(), 0.0f);
                        std::fill(simulationContext2D.record_vz.begin(), simulationContext2D.record_vz.end(), 0.0f);
                    }
                }
            }
        }
        else {

            if (ImGui::IsMouseDown(0)&&!ImGui::IsMouseDown(ImGuiMouseButton_Middle)&&!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                int  r_cells = static_cast<int>(state.brushRadius);
                bool model_changed = false;

                float p_vp = 3000.0f, p_vs = 1732.0f, p_rho = 2200.0f;
                if (state.brushType == TOOL_HIGH) {
                    p_vp = 4800.0f; p_vs = 2800.0f; p_rho = 2500.0f;
                }
                else if (state.brushType == TOOL_LOW) {
                    p_vp = 1500.0f; p_vs = 800.0f;  p_rho = 1800.0f;
                }
                else if (state.brushType == TOOL_WALL) {
                    p_vp = editVp2D; p_vs = editVs2D;  p_rho = editDensity2D;
                }
                else if (state.brushType == TOOL_ERASER) {
                    p_vp = 3000.0f; p_vs = 1732.0f; p_rho = 2200.0f;
                }

                for (int dy = -r_cells; dy <= r_cells; ++dy) {
                    for (int dx = -r_cells; dx <= r_cells; ++dx) {
                        if (dx * dx + dy * dy <= r_cells * r_cells) {
                            int target_x = mx + dx;
                            int target_y = my + dy;

                            if (target_x >= 0 && target_x < simulationContext2D.NX && target_y >= 0 && target_y < simulationContext2D.NZ) {

                                setGridMaterial2D(target_x, target_y, p_vp, p_vs, p_rho);
                                model_changed = true;
                            }
                        }
                    }
                }

                if (model_changed) {
                    uploadMaterialToGpu();
                }
            }
        }

    }

    if (canvasHovered2D||panActive2D) {
        {

            float winAspect = (float)winW / (float)winH;
            float simAspect = (float)viewWidth2D() / (float)viewHeight2D();
            ImVec2 aspectCorr = { 1.0f, 1.0f };
            if (winAspect > simAspect) {
                aspectCorr.x = winAspect / simAspect;
            }
            else {
                aspectCorr.y = simAspect / winAspect;
            }

            if (io.MouseWheel != 0) {
                float mouseSpeed = 0.1f * viewZoom2D;
                viewZoom2D += io.MouseWheel * mouseSpeed;
                if (viewZoom2D < 0.1f)   viewZoom2D = 0.1f;
                if (viewZoom2D > 100.0f) viewZoom2D = 100.0f;
            }

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)||ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                viewOffset2D.x -= 2.0f * io.MouseDelta.x * aspectCorr.x / (winW * viewZoom2D);
                viewOffset2D.y += 2.0f * io.MouseDelta.y * aspectCorr.y / (winH * viewZoom2D);
            }
        }

        ImVec2 mousePos = ImGui::GetMousePos();
        mousePos.x-=canvasOrigin2D.x;mousePos.y-=canvasOrigin2D.y;

        isMouseInside2D = (mousePos.x >= 0.0f && mousePos.x < (float)winW &&
            mousePos.y >= barHeight && mousePos.y < (float)(winH - barHeight));

        if (!canvasHovered2D) {
            isMouseInside2D = false;
        }

        hoverValid2D = false;
        if (isMouseInside2D) {
            float normX = mousePos.x / (float)winW;
            float normY = mousePos.y / (float)winH;

            float worldX = (normX - 0.5f) * (aspectCorr.x / viewZoom2D) + 0.5f + (0.5f * viewOffset2D.x);
            float worldY = (normY - 0.5f) * (aspectCorr.y / viewZoom2D) + 0.5f - (0.5f * viewOffset2D.y);

            hoverMx2D = int(std::floor(worldX * viewWidth2D()))+viewOrigin2D();
            hoverMy2D = int(std::floor(worldY * viewHeight2D()))+viewOriginZ2D();

            if (hoverMx2D >= 0 && hoverMx2D < simulationContext2D.NX && hoverMy2D >= 0 && hoverMy2D < simulationContext2D.NZ) {
                int k = hoverMy2D * simulationContext2D.NX + hoverMx2D;
                hoverRho2D = simulationContext2D.rho[k];
                if (hoverRho2D > 0.0f) {
                    hoverVp2D = sqrtf(simulationContext2D.lambda2mu[k] / hoverRho2D);
                    hoverVs2D = sqrtf(simulationContext2D.mu[k] / hoverRho2D);
                    hoverValid2D = true;
                }
            }
        }
    }
}

/** Advances configured CUDA time steps and coordinates recording, replay capture, and run state. */
inline void updateWavefieldSimulation(SimState& state) {
    ImGuiIO& io = ImGui::GetIO();
    bool     can_run = state.running && (infiniteMode2D || currentIt2D < simulationContext2D.nt);

    if (can_run) {
        elapsedRunWallTime2D += io.DeltaTime;

        for (int s = 0; s < stepsPerFrame2D; ++s) {
            if(!state.running) break;
            if (infiniteMode2D || currentIt2D < simulationContext2D.nt) {

                if (multiSourceMode2D && !activeSources2D.empty()) {
                    for (auto it = activeSources2D.begin(); it != activeSources2D.end(); ) {
                        it->t += simulationContext2D.dt;
                        float t_peak = 1.0f / it->f_peak;
                        if (it->t > 2.5f * t_peak) {
                            it = activeSources2D.erase(it);
                        }
                        else {
                            ++it;
                        }
                    }
                    if (!activeSources2D.empty()) {
                        if (activeSources2D.size() > kMaximumActiveSources) throw std::invalid_argument("Active source capacity exceeded");
                        uploadActiveSources2D(gpuData2D,activeSources2D);
                    }
                }

                const auto stepStart=std::chrono::steady_clock::now();
                runGpuSimulationStep(gpuData2D, currentIt2D, simulationContext2D, multiSourceMode2D ? (int)activeSources2D.size() : -1);
                if(!gpuData2D.cpu) CUDA_CHECK(cudaDeviceSynchronize());
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stepStart).count();
                stepMilliseconds2D=stepMilliseconds2D>0?.95*stepMilliseconds2D+.05*ms:ms;
                stepsPerSecond2D=1000/std::max(stepMilliseconds2D,1e-9);

                if (acquisition2D.isRecording) {

                    const int steps_per_sample = acquisition2D.samplingStepStride;
                    if (steps_per_sample <= 0 || std::abs(steps_per_sample * simulationContext2D.dt - acquisition2D.actualSampleInterval) > acquisition2D.actualSampleInterval * 1e-5f) {
                        throw std::invalid_argument("Simulation dt changed during acquisition; start a new recording");
                    }

                    const int recordingStep=currentIt2D-acquisition2D.captureStartStep;
                    if(recordingStep<0) throw std::invalid_argument("Recording timeline was reset; start a new capture.");
                    if (recordingStep % steps_per_sample == 0) {
                        int sample_idx = recordingStep / steps_per_sample;
                        if (acquisition2D.numChannels <= 0 || acquisition2D.recorded_vx.size() != acquisition2D.numChannels || acquisition2D.recorded_vz.size() != acquisition2D.numChannels) throw std::invalid_argument("Recording buffers do not match receiver channels");
                        int max_samples = static_cast<int>(acquisition2D.recorded_vz[0].size());

                        if (sample_idx < max_samples) {

                            static std::vector<float> h_step_vx;
                            h_step_vx.resize(acquisition2D.numChannels);
                            static std::vector<float> h_step_vz;
                            h_step_vz.resize(acquisition2D.numChannels);
                            recordReceiverStepOnGpu(gpuData2D, acquisition2D.numChannels, h_step_vx.data(), h_step_vz.data());

                            for (int c = 0; c < acquisition2D.numChannels; ++c) {
                                acquisition2D.recorded_vx[c][sample_idx] = h_step_vx[c];
                                acquisition2D.recorded_vz[c][sample_idx] = h_step_vz[c];
                            }
                            acquisition2D.capturedSamples=sample_idx+1;
                        }
                    }

                    acquisition2D.totalTimer = (recordingStep+1) * simulationContext2D.dt;
                    if (autoAcquisition2D.state != AUTO_ACQ_RUNNING) {

                        if (acquisition2D.totalTimer >= acquisition2D.totalDurationSec) {
                            acquisition2D.isRecording = false;
                            state.running = false;

                            acquisitionAction2D([]{exportCapturePackage2D();});
                        }
                    }
                }

                if (replay2D.state == REPLAY_RECORDING) {

                    if (currentIt2D % replay2D.recordStride == 0) {
                        if (replay2D.frames.size() < static_cast<size_t>(replay2D.maxFrames)) {
                            std::vector<float> h_vz(simulationContext2D.total_grid);

                            copyWavefieldToHost(gpuData2D, h_vz.data());
                            replay2D.frames.push_back(h_vz);
                        }
                        else {

                            replay2D.state = REPLAY_IDLE;
                        }
                    }
                }

                currentIt2D++;
            }
        }
    }
    if(!infiniteMode2D&&currentIt2D>=simulationContext2D.nt) {
        state.running=false;
        if(acquisition2D.isRecording&&autoAcquisition2D.state!=AUTO_ACQ_RUNNING) {
            acquisition2D.isRecording=false;
            acquisitionAction2D([]{exportCapturePackage2D();});
        }
    }
}

/** Advances the multi-shot acquisition state machine, resetting, stepping, and exporting each shot. */
inline void updateAutoAcquisitionWorkflow(SimState& state, GLHandles& gl) {
    if (autoAcquisition2D.state != AUTO_ACQ_RUNNING) return;

    if (currentIt2D >= simulationContext2D.nt) {

        acquisition2D.currentFileIndex=autoAcquisition2D.currentShotIdx+1;
        if(!acquisitionAction2D([]{exportCapturePackage2D(false);})) {
            autoAcquisition2D.state=AUTO_ACQ_IDLE;acquisition2D.isRecording=false;state.running=false;return;
        }

        autoAcquisition2D.currentShotIdx++;

        if (autoAcquisition2D.currentShotIdx < autoAcquisition2D.totalShots) {

            int next_x = autoAcquisition2D.startX + autoAcquisition2D.currentShotIdx * autoAcquisition2D.shotIntervalX;
            editSrcX2D = std::clamp(next_x, 1, simulationContext2D.NX - 2);
            editSrcZ2D = autoAcquisition2D.shotDepthZ;

            triggerSingleShot(gl, state);

            acquisition2D.initBuffer();
            acquisition2D.isRecording = true;
            state.running = true;

            std::cout << "[Acquisition] Automatically moving to Shot " << autoAcquisition2D.currentShotIdx + 1
                << " at Grid X: " << editSrcX2D << std::endl;
        }
        else {

            autoAcquisition2D.state = AUTO_ACQ_FINISHED;
            state.running = false;
            acquisition2D.isRecording = false;

            popupMessage2D = "All " + std::to_string(autoAcquisition2D.totalShots) + " shot packages saved in: " + pathToUtf8(activeExportDirectory2D);
            showSuccessPopup2D = true;
        }
    }
}

/** Maps the current CUDA wavefield texture onto the application viewport with aspect correction. */
inline void renderFullBackbufferWavefield(int winW, int winH, float barHeight, GLHandles& gl) {
    glBindTexture(GL_TEXTURE_2D,gl.seisTex);
    GLint textureWidth=0,textureHeight=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&textureWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&textureHeight);
    if(textureWidth!=gpuData2D.NX||textureHeight!=gpuData2D.NZ) {
        if(gl.cudaSeisRes) CUDA_CHECK(cudaGraphicsUnregisterResource(gl.cudaSeisRes));
        gl.cudaSeisRes=nullptr;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,gpuData2D.NX,gpuData2D.NZ,0,GL_RGBA,GL_FLOAT,nullptr);

        updateModelTexture();
    }


    if(gpuData2D.cpu) {
        if(gl.cudaSeisRes) {CUDA_CHECK(cudaGraphicsUnregisterResource(gl.cudaSeisRes));gl.cudaSeisRes=nullptr;}
        static std::vector<float4> pixels;pixels.resize(gpuData2D.total_grid);
        currentWaveReference2D=generateScientificWavefield2D(gpuData2D,pixels.data(),colorScale2D,showComponent2D,automaticWaveScale2D);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,gpuData2D.NX,gpuData2D.NZ,GL_RGBA,GL_FLOAT,pixels.data());
    } else {
        if(!gl.cudaSeisRes) {
            auto result=cudaGraphicsGLRegisterImage(&gl.cudaSeisRes,gl.seisTex,GL_TEXTURE_2D,cudaGraphicsRegisterFlagsWriteDiscard);
            if(result!=cudaSuccess) {
                backendStatus2D="CUDA/OpenGL interop unavailable: switched to CPU / OpenMP";
                selectedBackend2D=1;resetSimRequested2D=true;return;
            }
        }
    CUDA_CHECK(cudaGraphicsMapResources(1, &gl.cudaSeisRes, 0));
    struct MappedResourceGuard {
        cudaGraphicsResource_t resource;
        ~MappedResourceGuard() {
            if (resource) cudaGraphicsUnmapResources(1, &resource, 0);
        }
    } mappedResourceGuard{gl.cudaSeisRes};
    CUDA_CHECK(cudaGraphicsSubResourceGetMappedArray(&gl.cudaSeisArray, gl.cudaSeisRes, 0, 0));

    float4*& d_rgba_out = gl.deviceWavefieldRgba;
    int& last_size = gl.wavefieldRgbaCapacity;

    if (last_size != gpuData2D.total_grid) {
        if (d_rgba_out) cudaFree(d_rgba_out);
        d_rgba_out = nullptr;
        CUDA_CHECK(cudaMalloc(&d_rgba_out, static_cast<size_t>(gpuData2D.total_grid) * sizeof(float4)));
        last_size = gpuData2D.total_grid;
    }
    currentWaveReference2D=generateScientificWavefield2D(gpuData2D,d_rgba_out,colorScale2D,showComponent2D,automaticWaveScale2D);

    CUDA_CHECK(cudaMemcpy2DToArray(
        gl.cudaSeisArray, 0, 0,
        d_rgba_out,
        gpuData2D.NX * sizeof(float4),
        gpuData2D.NX * sizeof(float4),
        gpuData2D.NZ,
        cudaMemcpyDeviceToDevice
    ));
    CUDA_CHECK(cudaGraphicsUnmapResources(1, &gl.cudaSeisRes, 0));
    mappedResourceGuard.resource = nullptr;

    }
    glViewport(0, 0, winW, winH);
    glUseProgram(gl.seisProg);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.seisTex);
    glUniform1i(glGetUniformLocation(gl.seisProg, "seisTexture"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, modelTex2D);
    glUniform1i(glGetUniformLocation(gl.seisProg, "modelTexture"), 1);

    glUniform1i(glGetUniformLocation(gl.seisProg, "waveStyle"), waveStyle2D);
    glUniform3fv(glGetUniformLocation(gl.seisProg,"waveStops"),5,Seismic::wavePalette(waveStyle2D)[0].data());
    glUniform1i(glGetUniformLocation(gl.seisProg,"showWave"),showWave2D);
    glUniform1i(glGetUniformLocation(gl.seisProg,"showMesh"),showModelMesh2D);
    glUniform1f(glGetUniformLocation(gl.seisProg,"meshOpacity"),meshOpacity2D);
    glUniform1i(glGetUniformLocation(gl.seisProg,"showMaterial"),showMaterial2D);
    glUniform1f(glGetUniformLocation(gl.seisProg,"waveOpacity"),waveOpacity2D);
    const auto& theme=AppUi::palette().panel;
    glUniform3f(glGetUniformLocation(gl.seisProg,"themeBackground"),theme.x,theme.y,theme.z);
    glUniform1i(glGetUniformLocation(gl.seisProg, "modelStyle"), modelStyle2D);
    glUniform1i(glGetUniformLocation(gl.seisProg, "showGrid"), showGrid2D ? 1 : 0);

    glUniform1i(glGetUniformLocation(gl.seisProg, "lightCanvas2D"),AppUi::palette().light||modelStyle2D==4);
    glUniform1f(glGetUniformLocation(gl.seisProg, "gridSpacing"), uiGridSpacing2D / simulationContext2D.h);
    glUniform1f(glGetUniformLocation(gl.seisProg, "gridOpacity"), uiGridOpacity2D);

    glUniform1f(glGetUniformLocation(gl.seisProg, "uniformVp"), editVp2D);
    glUniform1f(glGetUniformLocation(gl.seisProg, "uniformRho"), editDensity2D);
    glUniform1i(glGetUniformLocation(gl.seisProg, "useModelTexture"), 1);

    glUniform2f(glGetUniformLocation(gl.seisProg, "winSize"), (float)winW, (float)winH);
    glUniform2f(glGetUniformLocation(gl.seisProg, "simSize"),float(viewWidth2D()),float(viewHeight2D()));
    glUniform2f(glGetUniformLocation(gl.seisProg,"storageSize"),float(gpuData2D.NX),float(gpuData2D.NZ));
    glUniform2f(glGetUniformLocation(gl.seisProg,"physicalSize"),float(simulationContext2D.NX-1),float(simulationContext2D.NZ-1));
    glUniform2f(glGetUniformLocation(gl.seisProg,"physicalOrigin"),float(domainPad2D())+.5f,float(domainPad2D())+.5f);
    glUniform2f(glGetUniformLocation(gl.seisProg,"storageViewOrigin"),showBoundary2D?.5f:float(domainPad2D())+.5f,showBoundary2D&&!freeTop2D()?.5f:float(domainPad2D())+.5f);
    glUniform2f(glGetUniformLocation(gl.seisProg, "viewOffset"), viewOffset2D.x, viewOffset2D.y);
    glUniform1f(glGetUniformLocation(gl.seisProg, "viewZoom"), viewZoom2D);
    glUniform1f(glGetUniformLocation(gl.seisProg, "totalTime"), (float)glfwGetTime());
    glUniform1f(glGetUniformLocation(gl.seisProg, "npml"), (float)simulationContext2D.npml);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.seisTex);
    glUniform1i(glGetUniformLocation(gl.seisProg, "seisTexture"), 0);

    glBindVertexArray(gl.quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/** Draws simulation controls, run state, acquisition controls, and device telemetry over the wavefield. */
