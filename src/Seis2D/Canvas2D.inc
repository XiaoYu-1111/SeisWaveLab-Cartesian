// Private 2D implementation, compiled only through Simulation2D.cpp.
// Draw physical ruler labels behind tool windows, preserving the model-to-screen mapping.
inline void renderGridRulerOnBackbuffer(
    const SimulationContext& simulationContext2D,
    const SimState& state,
    int                      winW,
    int                      winH,
    float                    barHeight,
    const ImVec2& viewOffset2D,
    float                    viewZoom2D
) {
    ImDrawList* draw_list = canvasDrawList2D;
    ImU32       textCol = AppUi::color(AppUi::palette().muted);
    ImU32       tickCol = AppUi::color(AppUi::palette().muted,.5f);

    float  winAspect = (float)winW / (float)winH;
    float  simAspect = float(viewWidth2D())/viewHeight2D();
    ImVec2 aspectCorr = { 1.0f, 1.0f };

    if (winAspect > simAspect) {
        aspectCorr.x = winAspect / simAspect;
    }
    else {
        aspectCorr.y = simAspect / winAspect;
    }

    auto calcNiceInterval = [&](float target_pixel_dist, float total_cells, float aspect_corr, float win_dim) -> float {

        float target_phys = (target_pixel_dist * total_cells * aspect_corr * simulationContext2D.h) / (win_dim * viewZoom2D);
        if (target_phys <= 0.0f) return 10.0f;

        const float baseSpacing=std::max(0.001f,uiGridSpacing2D);
        const float required=std::max(1.0f,target_phys/baseSpacing);
        const float decade=powf(10.0f,floorf(log10f(required)));
        const float ratio=required/decade;
        const float multiplier=ratio<=1?1:ratio<=2?2:ratio<=5?5:10;
        return baseSpacing*decade*multiplier;
        };

    float nice_interval_x = calcNiceInterval(100.0f, viewWidth2D(), aspectCorr.x, (float)winW);
    float nice_interval_y = calcNiceInterval(100.0f, viewHeight2D(), aspectCorr.y, (float)winH);

    float total_width_m = (simulationContext2D.NX-1) * simulationContext2D.h;
    for (float physX = 0.0f; physX <= total_width_m; physX += nice_interval_x) {

        float simX = physX / simulationContext2D.h;
        float aPos_x = ((simX-viewOrigin2D()) / viewWidth2D()) * 2.0f - 1.0f;
        float Pndc_x = (aPos_x - viewOffset2D.x) * viewZoom2D / aspectCorr.x;
        float screenX = (Pndc_x * 0.5f + 0.5f) * winW;

        if (screenX >= 0.0f && screenX <= (float)winW) {
            float screenY = float(winH)+1.0f;
            draw_list->AddLine(ImVec2(canvasOrigin2D.x+screenX,canvasOrigin2D.y+screenY), ImVec2(canvasOrigin2D.x+screenX,canvasOrigin2D.y+screenY+6.0f), tickCol, 1.5f);

            char buf[32];
            snprintf(buf, 32, "%.4gm", physX);

            ImVec2 txtSize = AppUi::Widgets::CalcTextSize(buf);
            float  offsetX = -txtSize.x * 0.5f;

            ImVec2 pos(canvasOrigin2D.x+std::clamp(screenX+offsetX,0.0f,std::max(0.0f,winW-txtSize.x)),canvasOrigin2D.y+screenY+8.0f);
            draw_list->AddRectFilled(ImVec2(pos.x-2,pos.y-1),ImVec2(pos.x+txtSize.x+2,pos.y+txtSize.y+1),AppUi::color(AppUi::palette().background,.94f));
            if (!AppUi::palette().light) AppUi::Widgets::drawText(draw_list,ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), buf);
            AppUi::Widgets::drawText(draw_list,pos, textCol, buf);
        }
    }

    float total_depth_m = (simulationContext2D.NZ-1) * simulationContext2D.h;
    for (float physY = 0.0f; physY <= total_depth_m; physY += nice_interval_y) {
        float simY = physY / simulationContext2D.h;
        float aPos_y = 1.0f - ((simY-viewOriginZ2D()) / viewHeight2D()) * 2.0f;
        float Pndc_y = (aPos_y - viewOffset2D.y) * viewZoom2D / aspectCorr.y;
        float screenY = (0.5f - Pndc_y * 0.5f) * winH;

        if (screenY >= barHeight && screenY <= (float)winH) {
            float screenX = -8.0f;
            draw_list->AddLine(ImVec2(canvasOrigin2D.x+screenX,canvasOrigin2D.y+screenY), ImVec2(canvasOrigin2D.x+screenX+6.0f,canvasOrigin2D.y+screenY), tickCol, 1.5f);

            char buf[32];
            snprintf(buf, 32, "%.4gm", physY);

            ImVec2 txtSize = AppUi::Widgets::CalcTextSize(buf);
            ImVec2 pos(canvasOrigin2D.x-txtSize.x-12.0f,canvasOrigin2D.y+std::clamp(screenY-txtSize.y*.5f,0.0f,std::max(0.0f,winH-txtSize.y)));

            draw_list->AddRectFilled(ImVec2(pos.x-2,pos.y-1),ImVec2(pos.x+txtSize.x+2,pos.y+txtSize.y+1),AppUi::color(AppUi::palette().background,.94f));
            if (!AppUi::palette().light) AppUi::Widgets::drawText(draw_list,ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), buf);
            AppUi::Widgets::drawText(draw_list,pos, textCol, buf);
        }
    }
}

inline void renderBrushCursor(const SimState& state, const ViewportInfo& vp) {
    ImVec2 mousePos = ImGui::GetMousePos();
    bool   isInside = (mousePos.x >= vp.x && mousePos.x < (vp.x + vp.w) &&
        mousePos.y >= vp.y && mousePos.y < (vp.y + vp.h));

    if (!canvasHovered2D || acquisition2D.isRecording) return;

    if (isInside) {
        ImDrawList* fg = canvasDrawList2D;

        if (state.brushType == TOOL_NONE) {
            float t = (float)ImGui::GetTime();
            const float crosshairSize = 18.0f;
            const float outerRingRadius = 35.0f;

            float r = uiAccent2D.x;
            float g = uiAccent2D.y;
            float b = uiAccent2D.z;

            ImU32 col_main = IM_COL32(r * 255, g * 255, b * 255, 255);

            ImU32 col_dynamic = IM_COL32(
                (int)std::min(255.0f, r * 1.15f * 255.0f),
                (int)std::min(255.0f, g * 1.15f * 255.0f),
                (int)std::min(255.0f, b * 1.15f * 255.0f),
                255
            );

            ImU32 col_shadow = IM_COL32(10, 18, 12, 160);

            fg->AddLine(ImVec2(mousePos.x - crosshairSize, mousePos.y), ImVec2(mousePos.x + crosshairSize, mousePos.y), col_shadow, 4.0f);
            fg->AddLine(ImVec2(mousePos.x, mousePos.y - crosshairSize), ImVec2(mousePos.x, mousePos.y + crosshairSize), col_shadow, 4.0f);

            fg->AddLine(ImVec2(mousePos.x - crosshairSize, mousePos.y), ImVec2(mousePos.x + crosshairSize, mousePos.y), col_main, 2.0f);
            fg->AddLine(ImVec2(mousePos.x, mousePos.y - crosshairSize), ImVec2(mousePos.x, mousePos.y + crosshairSize), col_main, 2.0f);

            fg->AddCircle(mousePos, outerRingRadius, col_main, 32, 1.5f);

            float  angle = t * 4.0f;
            ImVec2 scanEnd = ImVec2(mousePos.x + cos(angle) * outerRingRadius,
                mousePos.y + sin(angle) * outerRingRadius);
            fg->AddLine(mousePos, scanEnd, col_dynamic, 2.0f);
            fg->AddCircleFilled(scanEnd, 3.5f, col_dynamic);

            float pulse_t = fmodf(t, 1.5f) / 1.5f;
            float pulse_radius = pulse_t * outerRingRadius;
            int   pulse_alpha = (int)(sin(pulse_t * 3.14159f) * 120);

            ImU32 col_pulse = IM_COL32(r * 255, g * 255, b * 255, pulse_alpha);
            fg->AddCircle(mousePos, pulse_radius, col_pulse, 32, 3.0f);
        }
        else {
            ImU32 brushColor;
            switch (state.brushType) {
            case TOOL_HIGH:   brushColor = IM_COL32(255, 100, 100, 200); break;
            case TOOL_LOW:    brushColor = IM_COL32(100, 150, 255, 200); break;
            case TOOL_WALL:   brushColor = IM_COL32(255, 255, 0, 200);   break;
            case TOOL_ERASER: brushColor = IM_COL32(200, 200, 200, 200); break;
            default:          brushColor = IM_COL32(255, 255, 255, 200); break;
            }

            float screenRadius = state.brushRadius * vp.scaleX;
            fg->AddCircle(mousePos, screenRadius, brushColor, 0, 2.0f);
            fg->AddCircle(mousePos, screenRadius - 1.0f, IM_COL32(0, 0, 0, 150), 0, 1.0f);
            fg->AddCircleFilled(mousePos, 3.0f, IM_COL32(255, 255, 255, 255));
        }
    }
}

inline void renderSourceMarker(
    const SimulationContext& simulationContext2D,
    int                      src_x,
    int                      src_z,
    int                      winW,
    int                      winH,
    float                    barHeight,
    const ImVec2& viewOffset2D,
    float                    viewZoom2D
) {
    float  winAspect = (float)winW / (float)winH;
    float  simAspect = float(viewWidth2D())/viewHeight2D();
    ImVec2 aspectCorr = { 1.0f, 1.0f };

    if (winAspect > simAspect) {
        aspectCorr.x = winAspect / simAspect;
    }
    else {
        aspectCorr.y = simAspect / winAspect;
    }

    float aPos_x = ((float(src_x)-viewOrigin2D()) / viewWidth2D()) * 2.0f - 1.0f;
    float aPos_y = 1.0f - ((float(src_z)-viewOriginZ2D()) / viewHeight2D()) * 2.0f;

    float Pndc_x = (aPos_x - viewOffset2D.x) * viewZoom2D / aspectCorr.x;
    float screenX = (Pndc_x * 0.5f + 0.5f) * winW;

    float Pndc_y = (aPos_y - viewOffset2D.y) * viewZoom2D / aspectCorr.y;
    float screenY = (0.5f - Pndc_y * 0.5f) * winH;

    if (screenX >= 0.0f && screenX <= winW && screenY >= barHeight && screenY <= winH) {
        ImDrawList* fg = canvasDrawList2D;
        ImVec2      pos(canvasOrigin2D.x+screenX,canvasOrigin2D.y+screenY);

        fg->AddCircleFilled(pos,4.5f,IM_COL32(239,154,46,255));
        fg->AddCircle(pos,5.5f,AppUi::color(AppUi::palette().panel),24,1.2f);

    }
}

inline void renderSeisPopups() {

    if (showSuccessPopup2D) {
        ImGui::OpenPopup("Simulation Success");
        showSuccessPopup2D = false;
    }

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    if (ImGui::BeginPopupModal("Simulation Success", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        AppUi::Widgets::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ SUCCESS ]");
        ImGui::Separator();
        ImGui::Spacing();

        AppUi::Widgets::Text("%s", popupMessage2D.c_str());

        ImGui::Spacing();
        ImGui::Separator();

        if (AppUi::Widgets::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (showErrorPopup2D) {
        ImGui::OpenPopup("Simulation Error");
        showErrorPopup2D = false;
    }

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    if (ImGui::BeginPopupModal("Simulation Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        AppUi::Widgets::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[ ERROR ]");
        ImGui::Separator();
        ImGui::Spacing();

        AppUi::Widgets::Text("%s", popupMessage2D.c_str());

        ImGui::Spacing();
        ImGui::Separator();

        if (AppUi::Widgets::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

inline ImVec4 sampleWavePalette2D(float t) {
    const auto& stops=Seismic::wavePalette(waveStyle2D);
    const float x=std::clamp(t,0.0f,1.0f)*4;const int i=std::min(3,int(x));
    return {stops[i][0]+(stops[i+1][0]-stops[i][0])*(x-i),stops[i][1]+(stops[i+1][1]-stops[i][1])*(x-i),
        stops[i][2]+(stops[i+1][2]-stops[i][2])*(x-i),1};
}
inline void drawColorLegends2D() {
    auto* draw=ImGui::GetWindowDrawList();
    const auto bar=[&](bool wave) {
        const auto at=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
        const auto color=[&](float t) {
            if(wave||modelStyle2D==3) return sampleWavePalette2D(t);
            const float vp=cachedMinVp2D+(cachedMaxVp2D-cachedMinVp2D)*t;
            const float n=std::clamp((vp-500)/5500,0.0f,1.0f);
            if(modelStyle2D==4) {const float v=.965f+(.90f-.965f)*n*.35f;return ImVec4(v,v,v,1);}
            if(modelStyle2D==2) {const float v=.18f+.64f*n;return ImVec4(v,v,v,1);}
            if(modelStyle2D==0) return ImVec4(.012f,.015f,.018f,1);
            const ImVec4 stops[]={{0,.22f,.45f,1},{.28f,.55f,.42f,1},{.75f,.65f,.28f,1},{.68f,.18f,.12f,1}};
            const float x=std::min(n*3,2.999f);const int i=int(x);const float f=x-i;
            return ImVec4(stops[i].x+(stops[i+1].x-stops[i].x)*f,stops[i].y+(stops[i+1].y-stops[i].y)*f,stops[i].z+(stops[i+1].z-stops[i].z)*f,1);
        };
        for(int i=0;i<128;++i) draw->AddRectFilledMultiColor(ImVec2(at.x+width*i/128,at.y),ImVec2(at.x+width*(i+1)/128,at.y+8),
            AppUi::color(color(i/128.0f)),AppUi::color(color((i+1)/128.0f)),AppUi::color(color((i+1)/128.0f)),AppUi::color(color(i/128.0f)));
        ImGui::Dummy(ImVec2(width,10));
        if(wave) {
            const char* components[]={"|V|","Vx","Vz","Pressure","Sxz","Div","Curl"};
            if(showComponent2D<=2) AppUi::Widgets::Text("%s | -1 .. 1 | 1 = %.3e m/s",components[std::clamp(showComponent2D,0,6)],currentWaveReference2D);
            else AppUi::Widgets::Text("%s | normalized -1 .. 1 | gain %.3f",components[std::clamp(showComponent2D,0,6)],colorScale2D);
        } else AppUi::Widgets::Text("Vp: %.1f .. %.1f m/s",cachedMinVp2D,cachedMaxVp2D);
    };
    if(showWave2D) bar(true);
    if(showMaterial2D) bar(false);
}
