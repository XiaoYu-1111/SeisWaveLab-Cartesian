// Private 2D implementation, compiled only through Simulation2D.cpp.
// Build the selected synthetic medium and position its source using the current grid.
inline void loadScenario(int type) {
    int W = simulationContext2D.NX;
    int H = simulationContext2D.NZ;

    float defVp = 3000.0f, defVs = 1732.0f, defRho = 2200.0f;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            setMaterialAt(x, y, defVp, defVs, defRho);
        }
    }

    editSrcX2D = W / 2;
    editSrcZ2D = H / 2;

    if (type == SCENE_EARTH) {

        int   cx = W / 2;
        int   cy = H / 2;
        float maxRadius = (std::min(W, H) / 2.0f) * 0.95f;
        float r_core = maxRadius * 0.54f;
        float r_mantle = maxRadius * 0.92f;

        float r_core_sq = r_core * r_core;
        float r_mantle_sq = r_mantle * r_mantle;
        float max_sq = maxRadius * maxRadius;

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float dx = (float)(x - cx);
                float dy = (float)(y - cy);
                float distSq = dx * dx + dy * dy;

                if (distSq < r_core_sq) {
                    setMaterialAt(x, y, 4000.0f, 0.0f, 4000.0f);
                }
                else if (distSq < r_mantle_sq) {
                    setMaterialAt(x, y, 6000.0f, 3400.0f, 3000.0f);
                }
                else if (distSq < max_sq) {
                    setMaterialAt(x, y, 3000.0f, 1700.0f, 2500.0f);
                }
                else {
                    setMaterialAt(x, y, 340.0f, 10.0f, 100.0f);
                }
            }
        }
        editSrcX2D = W / 2;
        editSrcZ2D = H / 2;
    }
    else if (type == SCENE_DOUBLE_SLIT) {

        int cy = H / 2;
        int cx = W / 2;
        int wallX = cx;

        for (int i = 0; i < W * H; i++) {
            setMaterialAt(i % W, i / W, 1500.0f, 0.0f, 1000.0f);
        }

        for (int y = 0; y < H; ++y) {
            bool isSlit = (abs(y - cy) > 30 && abs(y - cy) < 60);
            if (!isSlit) {
                for (int w = 0; w < 10; w++) {
                    setMaterialAt(wallX + w, y, 5500.0f, 3200.0f, 8000.0f);
                }
            }
        }
        editSrcX2D = W / 4;
        editSrcZ2D = cy;
    }
    else if (type == SCENE_TWO_LAYER) {

        int interfaceY = H / 2;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (y < interfaceY) {
                    setMaterialAt(x, y, 4000.0f, 2300.0f, 2500.0f);
                }
                else {
                    setMaterialAt(x, y, 2000.0f, 1100.0f, 1500.0f);
                }
            }
        }
        editSrcX2D = W / 2;
        editSrcZ2D = H / 2 + 50;
    }
    else if (type == SCENE_WAVEGUIDE) {

        int cy = H / 2;
        int guideHalfWidth = 60;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (abs(y - cy) < guideHalfWidth) {
                    setMaterialAt(x, y, 2000.0f, 1000.0f, 1500.0f);
                }
                else {
                    setMaterialAt(x, y, 100.0f, 60.0f, 10000.0f);
                }
            }
        }
        editSrcX2D = 100;
        editSrcZ2D = cy;
    }
    else if (type == SCENE_WAVEGUIDE_CURVED) {

        int cy = H / 2;
        int guideHalfWidth = 60;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float currentCenterY = (H / 2) + (H / 4) * sin((float)x / W * 2.0f * 3.14159f);
                if (abs(y - currentCenterY) < guideHalfWidth) {
                    setMaterialAt(x, y, 2000.0f, 1000.0f, 1500.0f);
                }
                else {
                    setMaterialAt(x, y, 5500.0f, 3200.0f, 3000.0f);
                }
            }
        }
        editSrcX2D = 100;
        editSrcZ2D = H / 2;
    }
    else if (type == SCENE_CRYSTAL) {

        float bgVp = 1500.0f, bgVs = 0.0f, bgRho = 1000.0f;
        float scVp = 5800.0f, scVs = 3200.0f, scRho = 7800.0f;
        int   radius = 12, spacingX = 50, spacingY = 50;
        int   startX = 250, endX = W - 100, startY = 100, endY = H - 100;

        for (int i = 0; i < W * H; ++i) setMaterialAt(i % W, i / W, bgVp, bgVs, bgRho);

        int   cols = (endX - startX) / spacingX;
        int   rows = (endY - startY) / spacingY;
        float r2 = (float)(radius * radius);

        for (int j = 0; j < rows; ++j) {
            for (int i = 0; i < cols; ++i) {
                int cx = startX + i * spacingX;
                int cy = startY + j * spacingY;
                if (j % 2 == 1) cx += spacingX / 2;

                for (int y = cy - radius; y <= cy + radius; ++y) {
                    for (int x = cx - radius; x <= cx + radius; ++x) {
                        if (x >= 0 && x < W && y >= 0 && y < H) {
                            float dx = (float)(x - cx);
                            float dy = (float)(y - cy);
                            if (dx * dx + dy * dy <= r2) {
                                setMaterialAt(x, y, scVp, scVs, scRho);
                            }
                        }
                    }
                }
            }
        }
        editSrcX2D = 100;
        editSrcZ2D = H / 2;
    }
    else if (type == SCENE_RANDOM_SCATTER) {

        float bgVp = 2500.0f, bgVs = 1200.0f, bgRho = 2000.0f;
        float scVp = 4500.0f, scVs = 2500.0f, scRho = 2800.0f;

        for (int i = 0; i < W * H; ++i) setMaterialAt(i % W, i / W, bgVp, bgVs, bgRho);

        int numScatterers = 300;
        int minR = 5, maxR = 15;
        for (int k = 0; k < numScatterers; ++k) {
            int   cx = rand() % (W - 100) + 50;
            int   cy = rand() % (H - 100) + 50;
            int   r = rand() % (maxR - minR) + minR;
            float r2 = (float)(r * r);

            for (int y = cy - r; y <= cy + r; ++y) {
                for (int x = cx - r; x <= cx + r; ++x) {
                    if (x >= 0 && x < W && y >= 0 && y < H) {
                        float dx = x - cx;
                        float dy = y - cy;
                        if (dx * dx + dy * dy <= r2) {
                            setMaterialAt(x, y, scVp, scVs, scRho);
                        }
                    }
                }
            }
        }
        editSrcX2D = W / 2;
        editSrcZ2D = H / 2;
    }
    else if (type == SCENE_CURVED) {

        float l1_Vp = 2000.0f, l1_Vs = 1100.0f, l1_Rho = 1800.0f;
        float l2_Vp = 3500.0f, l2_Vs = 2000.0f, l2_Rho = 2400.0f;
        float l3_Vp = 5500.0f, l3_Vs = 3200.0f, l3_Rho = 2800.0f;

        float baseH1 = H * 0.35f, amp1 = H * 0.08f, freq1 = 2.0f * 3.14159f / W * 2.5f;
        float baseH2 = H * 0.65f, amp2 = H * 0.05f, freq2 = 2.0f * 3.14159f / W * 1.5f, phase2 = 1.0f;

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float h1 = baseH1 + amp1 * sin(x * freq1);
                float h2 = baseH2 + amp2 * sin(x * freq2 + phase2);
                if (y < h1)       setMaterialAt(x, y, l3_Vp, l3_Vs, l3_Rho);
                else if (y < h2)  setMaterialAt(x, y, l2_Vp, l2_Vs, l2_Rho);
                else              setMaterialAt(x, y, l1_Vp, l1_Vs, l1_Rho);
            }
        }
        editSrcX2D = W / 2;
        editSrcZ2D = H - 50;
    }
    else if (type == SCENE_REFRACTION) {

        float vTop = 1000.0f, vBot = 2000.0f;
        float rhoTop = 1800.0f, rhoBot = 2500.0f;
        for (int y = 0; y < H; ++y) {
            float ratio = (float)y / (float)H;
            float vp = vBot + (vTop - vBot) * ratio;
            float vs = vp / 1.732f;
            if (vp < 1000.0f) vs = 0.0f;
            float rho = rhoBot + (rhoTop - rhoBot) * ratio;

            for (int x = 0; x < W; ++x) setMaterialAt(x, y, vp, vs, rho);
        }
        editSrcX2D = 100;
        editSrcZ2D = H - 50;
    }
    else if (type == SCENE_PENROSE_ROOM) {

        float rockVp = 3000.0f, rockVs = 1732.0f, rockRho = 2500.0f;
        float airVp = 0.0f, airVs = 0.0f, airRho = 20.0f;
        float cx = W * 0.5f, cy = H * 0.5f;
        float s = std::min(W, H) * 0.38f;

        auto isInsidePenrose = [&](float px, float py) -> bool {
            float x = (px - cx) / s;
            float y = (py - cy) / s;
            float room_half_width = 1.5f;
            bool  top = (y < -0.4f) && ((x * x) / (room_half_width * room_half_width) + pow((y + 0.4f) / 0.7f, 2) <= 1.0f);
            bool  bottom = (y > 0.4f) && ((x * x) / (room_half_width * room_half_width) + pow((y - 0.4f) / 0.7f, 2) <= 1.0f);
            bool  middle_rect = (abs(y) <= 0.4f) && (abs(x) <= room_half_width);
            bool  room_base = top || bottom || middle_rect;

            float mush_pos_x = 1.3f;
            auto  is_mushroom = [&](float mx, float my) -> bool {
                bool cap = (mx > 0.0f) && (my * my / 0.16f + mx * mx / 0.09f <= 1.0f);
                bool stem = (mx <= 0.0f) && (abs(my) <= 0.08f) && (mx > -0.6f);
                return cap || stem;
                };
            bool left_mushroom = is_mushroom(x - (-mush_pos_x), y);
            bool right_mushroom = is_mushroom(-(x - mush_pos_x), y);
            return room_base && !left_mushroom && !right_mushroom;
            };

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (isInsidePenrose((float)x, (float)y)) setMaterialAt(x, y, rockVp, rockVs, rockRho);
                else                                     setMaterialAt(x, y, airVp, airVs, airRho);
            }
        }
        editSrcX2D = static_cast<int>(cx);
        editSrcZ2D = static_cast<int>(cy);
    }
    else if (type == SCENE_TOPOGRAPHY) {

        float rockVp = 2500.0f, rockVs = 1443.0f, rockRho = 2000.0f;

        float airVp = 340.0f, airVs = 0.0f, airRho = 50.0f;

        float baseHeight = H * 0.45f;
        float hillAmp = H * 0.15f;
        float hillFreq = 2.0f * 3.14159f / W * 1.5f;

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {

                float surfaceZ = baseHeight + hillAmp * sin(x * hillFreq);

                if (y > surfaceZ) {
                    setMaterialAt(x, y, airVp, airVs, airRho);
                }
                else {
                    setMaterialAt(x, y, rockVp, rockVs, rockRho);
                }
            }
        }

        editSrcX2D = W / 2;
        editSrcZ2D = H / 2;
        }
}

/** Stops the active run, rebuilds the selected model, refreshes coefficients, and reallocates GPU state. */
inline void applyScenario(int type, SimState& state) {
    state.running = false;
    currentIt2D = 0;
    elapsedRunWallTime2D = 0.0f;
    activeSources2D.clear();

    setupTestContext(simulationContext2D, parameters2D);

    loadScenario(type);

    updatePmlDampingArrays();
    autoAlignTimeStep();

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
}
