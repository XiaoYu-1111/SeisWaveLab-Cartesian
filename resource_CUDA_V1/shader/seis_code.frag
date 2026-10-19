#version 430 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D seisTexture;   // 地震波场颜色纹理 (CUDA零拷贝写入)
uniform sampler2D modelTexture;  // 离屏地质物性纹理 (R: Vp, G: Vs, B: Rho)
uniform bool useModelTexture;    // 是否启用非均匀速度模型纹理
uniform float uniformVp;         // 均匀介质下的纵波速度 (由 C++ 实时滑块传入)
uniform float uniformRho;        // 均匀介质下的密度 (由 C++ 实时滑块传入)

uniform vec2 simSize;
uniform vec2 storageSize,physicalSize,physicalOrigin,storageViewOrigin;
uniform float viewZoom;
uniform float totalTime;
uniform float npml;              // PML 边界格数
uniform int waveStyle;           // 0: Magma, 1~12 对应标准科学色谱
uniform int modelStyle;          // 背景地质图风格: 0: 经典钛金, 1: 科学地质图, 2: 灰度Vp, 3: 跟随波场风格
uniform bool showGrid;
uniform bool showWave,showMaterial,showMesh;
uniform float meshOpacity;
uniform float waveOpacity;
uniform vec3 waveStops[5],themeBackground;
uniform bool lightCanvas2D;

uniform float gridSpacing;       // 网格基础间隔 (如 50.0，对应 UI 的 50m 划分)
uniform float gridOpacity;       // 网格整体不透明度系数 (0.0 ~ 1.0 调节亮度)

// =============================================================================
// 【第一级：零依赖底层几何与差值辅助函数】
// =============================================================================
float getGridLine(float pos, float spacing, float pixelWidth) {
    float coord = pos / spacing;
    float df = max(fwidth(coord), 1e-5);
    float dist = abs(coord - round(coord));
    return 1.0 - smoothstep(pixelWidth, pixelWidth + 1.0, dist / df);
}

// 无分支 2D 矩形 SDF 距离函数 (无外部依赖，优先置顶)
float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// =============================================================================
// 【第二级：12 套标准的科学色谱单向定义】
// =============================================================================
vec3 getJet(float t) {
    vec3 c0 = vec3(0.0, 0.0, 0.5), c1 = vec3(0.0, 0.5, 1.0), c2 = vec3(0.5, 1.0, 0.5), c3 = vec3(1.0, 0.75, 0.0), c4 = vec3(0.5, 0.0, 0.0);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.5) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.5) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}

vec3 getTerrain(float t) {
    vec3 c0 = vec3(0.0, 0.235, 0.667), c1 = vec3(0.0, 0.706, 0.863), c2 = vec3(0.941, 0.941, 0.471), c3 = vec3(0.196, 0.784, 0.196), c4 = vec3(0.471, 0.392, 0.235), c5 = vec3(0.98, 0.98, 0.98);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.45) return mix(c1, c2, (t - 0.25) / 0.20);
    if (t < 0.60) return mix(c2, c3, (t - 0.45) / 0.15);
    if (t < 0.80) return mix(c3, c4, (t - 0.60) / 0.20);
    return mix(c4, c5, (t - 0.80) / 0.20);
}

vec3 getSeismic(float t) {
    vec3 c0 = vec3(0.000, 0.000, 0.706), c1 = vec3(0.88, 0.88, 0.88), c2 = vec3(0.706, 0.000, 0.000); // 更稳定的 phase center
    if (t < 0.5) return mix(c0, c1, t * 2.0);
    return mix(c1, c2, (t - 0.5) * 2.0);
}

vec3 getGrayscale(float t) {
    vec3 c0 = vec3(0.12), c1 = vec3(0.50), c2 = vec3(0.88);
    if (t < 0.5) return mix(c0, c1, t * 2.0);
    return mix(c1, c2, (t - 0.5) * 2.0);
}

vec3 getHot(float t) {
    vec3 c0 = vec3(0.0, 0.0, 0.0), c1 = vec3(1.0, 0.0, 0.0), c2 = vec3(1.0, 1.0, 0.0), c3 = vec3(1.0, 1.0, 1.0);
    if (t < 0.333) return mix(c0, c1, t * 3.0);
    if (t < 0.666) return mix(c1, c2, (t - 0.333) * 3.0);
    return mix(c2, c3, (t - 0.666) * 3.0);
}

vec3 getCold(float t) {
    vec3 c0 = vec3(0.0, 0.0, 0.0), c1 = vec3(0.0, 0.0, 1.0), c2 = vec3(0.0, 1.0, 1.0), c3 = vec3(1.0, 1.0, 1.0);
    if (t < 0.333) return mix(c0, c1, t * 3.0);
    if (t < 0.666) return mix(c1, c2, (t - 0.333) * 3.0);
    return mix(c2, c3, (t - 0.666) * 3.0);
}

vec3 getCoolwarm(float t) {
    vec3 c0 = vec3(0.230, 0.299, 0.754), c1 = vec3(0.554, 0.690, 0.996), c2 = vec3(0.865, 0.865, 0.865), c3 = vec3(0.957, 0.598, 0.477), c4 = vec3(0.706, 0.016, 0.150);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.5) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.5) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}

vec3 getViridis(float t) {
    vec3 c0 = vec3(0.267, 0.004, 0.329), c1 = vec3(0.282, 0.137, 0.455), c2 = vec3(0.251, 0.404, 0.541), c3 = vec3(0.208, 0.718, 0.475), c4 = vec3(0.561, 0.843, 0.267), c5 = vec3(0.85, 0.88, 0.18); // 压高亮
    if (t < 0.2) return mix(c0, c1, t * 5.0);
    if (t < 0.4) return mix(c1, c2, (t - 0.2) * 5.0);
    if (t < 0.6) return mix(c2, c3, (t - 0.4) * 5.0);
    if (t < 0.8) return mix(c3, c4, (t - 0.6) * 5.0);
    return mix(c4, c5, (t - 0.8) * 5.0);
}

vec3 getPlasma(float t) {
    vec3 c0 = vec3(0.051, 0.031, 0.529), c1 = vec3(0.416, 0.0, 0.659), c2 = vec3(0.733, 0.216, 0.329), c3 = vec3(0.976, 0.557, 0.035), c4 = vec3(0.941, 0.976, 0.129);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}

vec3 getTurbo(float t) {
    vec3 c0 = vec3(0.190, 0.071, 0.232), c1 = vec3(0.276, 0.421, 0.891), c2 = vec3(0.158, 0.735, 0.923); // deep purple, blue, cyan
    vec3 c3 = vec3(0.120, 0.780, 0.520); // green (降低亮度)
    vec3 c4 = vec3(0.580, 0.820, 0.180); // yellow-green (降低黄区过曝)
    vec3 c5 = vec3(0.930, 0.720, 0.180), c6 = vec3(0.980, 0.420, 0.120), c7 = vec3(0.480, 0.015, 0.015); // orange, red-orange, deep red
    if (t < 0.10) return mix(c0, c1, t * 10.0);
    if (t < 0.20) return mix(c1, c2, (t - 0.10) * 10.0);
    if (t < 0.35) return mix(c2, c3, (t - 0.20) / 0.15);
    if (t < 0.50) return mix(c3, c4, (t - 0.35) / 0.15);
    if (t < 0.70) return mix(c4, c5, (t - 0.50) / 0.20);
    if (t < 0.85) return mix(c5, c6, (t - 0.70) / 0.15);
    return mix(c6, c7, (t - 0.85) / 0.15);
}

vec3 getInferno(float t) {
    vec3 c0 = vec3(0.0, 0.0, 0.016), c1 = vec3(0.341, 0.063, 0.427), c2 = vec3(0.733, 0.216, 0.329), c3 = vec3(0.976, 0.557, 0.035), c4 = vec3(0.988, 1.0, 0.643);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}

vec3 getPuOr(float t) {
    vec3 c0 = vec3(0.498, 0.231, 0.031), c1 = vec3(0.775, 0.518, 0.235), c2 = vec3(0.992, 0.878, 0.714);
    vec3 c3 = vec3(0.92, 0.92, 0.90);
    vec3 c4 = vec3(0.847, 0.855, 0.922), c5 = vec3(0.592, 0.554, 0.745), c6 = vec3(0.329, 0.153, 0.533);
    if (t < 0.166) return mix(c0, c1, t * 6.0);
    if (t < 0.333) return mix(c1, c2, (t - 0.166) * 6.0);
    if (t < 0.5) return mix(c2, c3, (t - 0.333) * 6.0);
    if (t < 0.666) return mix(c3, c4, (t - 0.5) * 6.0);
    if (t < 0.833) return mix(c4, c5, (t - 0.666) * 6.0);
    return mix(c5, c6, (t - 0.833) * 6.0);
}

// =============================================================================
// 【 核心重构：高度复用的全局 13 套标准科学色谱映射函数库 】 [1.2.7]
// =============================================================================
vec3 getColormap(int style, float t, vec3 midCol) {
    float position=clamp(t,0.0,1.0)*4.0;
    int stop=min(int(position),3);
    return mix(waveStops[stop],waveStops[stop+1],position-float(stop));
}

// =============================================================================
// 【第三级：物性取值与地层解析函数】
// =============================================================================
float calcVp(vec2 uv) {
    if (useModelTexture) return texture(modelTexture, uv).r * 6000.0; // 映射至 0~6000 m/s
    return uniformVp;
}

float calcRHO(vec2 uv) {
    if (useModelTexture) return texture(modelTexture, uv).b * 3000.0; // 映射至 0~3000 kg/m3
    return uniformRho;
}

// =============================================================================
// 【 核心重构：高度集成的地质背景生成函数 】
// =============================================================================
vec3 getGeologicalBackground(vec2 uv, vec2 cellPos) {
    // Velocity -> normalized scalar
    float vp = calcVp(uv);
    float t = clamp((vp - 500.0) / 5500.0, 0.0, 1.0);

    // Default Background
    vec3 bgCol = vec3(0.012, 0.015, 0.018);

    // Style 0: Dark Titanium
    if (modelStyle == 0) {
        bgCol = vec3(0.012, 0.015, 0.018);
    }
    // Style 1: Geological Map
    else if (modelStyle == 1) {
        vec3 cWater = vec3(0.0, 0.22, 0.45), cSed = vec3(0.28, 0.55, 0.42), cRock = vec3(0.75, 0.65, 0.28), cHard = vec3(0.68, 0.18, 0.12);
        if (t < 0.333) {
            bgCol = mix(cWater, cSed, t * 3.0);
        } else if (t < 0.666) {
            bgCol = mix(cSed, cRock, (t - 0.333) * 3.0);
        } else {
            bgCol = mix(cRock, cHard, (t - 0.666) * 3.0);
        }
        // Structural edge enhancement
        if (useModelTexture) {
            vec2 texelSize = 1.0 / storageSize;
            float vp_right = calcVp(uv + vec2(texelSize.x, 0.0));
            float vp_up = calcVp(uv + vec2(0.0, texelSize.y));
            float diff = abs(vp - vp_right) + abs(vp - vp_up);
            float edge = smoothstep(10.0, 50.0, diff);
            bgCol = mix(bgCol, vec3(0.05, 0.05, 0.08), edge * 0.35);
        }
    }
    // Style 2: Grayscale Velocity
    else if (modelStyle == 2) {
        bgCol = vec3(mix(0.18, 0.82, t));
    }
    // Style 3: Sync With Active Wave Colormap
    else if (modelStyle == 3) {
        bgCol = getColormap(waveStyle, t, vec3(0.015, 0.018, 0.022));
        // Background desaturation 避免背景和波场竞争
        bgCol = mix(vec3(dot(bgCol, vec3(0.299, 0.587, 0.114))), bgCol, 0.55);
        // Dark scientific attenuation
        bgCol *= 0.38;
    }
    // Style 4: Pure White Scientific Background (Soft scientific white, 避免纯 1.0 白导致刺眼)
    else if (modelStyle == 4) {
        bgCol = mix(vec3(0.965), vec3(0.90), t * 0.35);
        // Very soft structural variation
        if (useModelTexture) {
            vec2 texelSize = 1.0 / storageSize;
            float vp_right = calcVp(uv + vec2(texelSize.x, 0.0));
            float vp_up = calcVp(uv + vec2(0.0, texelSize.y));
            float diff = abs(vp - vp_right) + abs(vp - vp_up);
            float edge = smoothstep(10.0, 50.0, diff);
            bgCol = mix(bgCol, vec3(0.82, 0.84, 0.88), edge * 0.12);
        }
    }

    // Actual external absorbing cells, never an overlay inside the physical model.
    vec2 outside=max(max(physicalOrigin-cellPos,cellPos-(physicalOrigin+physicalSize)),vec2(0));
    float distance=max(outside.x,outside.y);
    if(distance>0.0) {
        float fade=clamp(distance/max(npml,1.0),0.0,1.0);
        vec3 boundaryTint=modelStyle==4?vec3(.70,.76,.75):vec3(.05,.12,.13);
        bgCol=mix(bgCol,boundaryTint,.35+.35*fade);
    }
    return bgCol;
}

// =============================================================================
// 【第四级：主渲染 main() 函数，完全复用并精简，实现极致性能】
// =============================================================================
void main() {
    // 1. 垂直翻转纹理坐标 Y (地表居顶对齐)
    vec2 flippedTexCoords = (storageViewOrigin+vec2(TexCoords.x,1.0-TexCoords.y)*simSize)/storageSize;

    // 2. 采样地震波场颜色
    vec4 texColor = texture(seisTexture, flippedTexCoords);

    // 3. 将纹理坐标映射到实际网格单位
    vec2 cellPos = flippedTexCoords * storageSize;

    // 4. 计算自适应地质背景底色
    vec3 baseBG = showMaterial ? getGeologicalBackground(flippedTexCoords, cellPos) : themeBackground;

    // 5. 逆向物理重构：解构振幅值 v
    float v = texColor.g; // signed normalized amplitude, transported in float32

    // Same signed palette mapping and amplitude-proportional overlay as the curved-grid viewer.
    float normalized=clamp(v,-1.0,1.0);
    vec3 waveColor=getColormap(waveStyle,normalized*.5+.5,baseBG);
    float waveIntensity=showWave?clamp(abs(normalized)*4.0,0.0,1.0)*waveOpacity:0.0;

    // 最终混合：无波场区域完全保留高保真地层地质图，有波场区域叠加标准波谱
    vec3 finalColor = mix(baseBG, waveColor, waveIntensity);
    float pxToGridUnits = max(fwidth(cellPos.x), fwidth(cellPos.y));

    // Pixel distances keep borders equally sharp in X/Z, at any zoom or aspect ratio.
    vec2 pixelSize = max(fwidth(cellPos), vec2(1e-5));
    bool lightModel = dot(baseBG, vec3(0.299, 0.587, 0.114)) > 0.5;
    vec3 gridColor = lightModel ? vec3(0.25, 0.36, 0.38) : vec3(0.70, 0.83, 0.82);
    float outerDistance = min(min((cellPos.x-.5) / pixelSize.x, (storageSize.x-.5-cellPos.x) / pixelSize.x),
                              min((cellPos.y-.5) / pixelSize.y, (storageSize.y-.5-cellPos.y) / pixelSize.y));
    vec2 low = physicalOrigin, high = physicalOrigin+physicalSize;
    vec2 outside = max(max(low-cellPos, cellPos-high), vec2(0)) / pixelSize;
    vec2 side = min(abs(cellPos-low), abs(cellPos-high)) / pixelSize;
    float pmlDistance = length(outside) > 0.0 ? length(outside) : min(side.x,side.y);
    float borderDistance = npml > 0.0 ? min(outerDistance,pmlDistance) : outerDistance;
    if (showGrid) {
        // Same 1/2/5 hierarchy as the physical rulers; spacing is in cells.
        float baseSpacing = max(0.001, gridSpacing);
        float required = max(1.0, 100.0 * pxToGridUnits / baseSpacing);
        float decade = pow(10.0, floor(log(required) / log(10.0)));
        float ratio = required / decade;
        float multiplier = ratio <= 1.0 ? 1.0 : ratio <= 2.0 ? 2.0 : ratio <= 5.0 ? 5.0 : 10.0;
        float mainInterval = baseSpacing * decade * multiplier;
        float subInterval = mainInterval / 5.0;
        float minorFade = smoothstep(10.0, 20.0, subInterval / pxToGridUnits);
        float mainX = getGridLine(cellPos.x-physicalOrigin.x, mainInterval, 0.45);
        float mainY = getGridLine(cellPos.y-physicalOrigin.y, mainInterval, 0.45);
        // Remove coincident minor strokes instead of stacking two line systems.
        float subX = getGridLine(cellPos.x-physicalOrigin.x, subInterval, 0.20) * (1.0-mainX);
        float subY = getGridLine(cellPos.y-physicalOrigin.y, subInterval, 0.20) * (1.0-mainY);
        float coverage = max(max(subX,subY)*0.13*minorFade,max(mainX,mainY)*0.38);
        float interior = npml > 0.0 ? step(low.x,cellPos.x)*step(low.y,cellPos.y)*step(cellPos.x,high.x)*step(cellPos.y,high.y) : 1.0;
        coverage *= gridOpacity * interior * smoothstep(2.5,4.5,borderDistance);
        finalColor = mix(finalColor,gridColor,coverage);
    }
    if(showMesh) {
        float visible=smoothstep(2.0,5.0,1.0/pxToGridUnits);
        vec2 physicalCell=cellPos-physicalOrigin;
        float inside=step(0.0,physicalCell.x)*step(0.0,physicalCell.y)*step(physicalCell.x,physicalSize.x)*step(physicalCell.y,physicalSize.y);
        float mesh=max(getGridLine(physicalCell.x,1.0,.12),getGridLine(physicalCell.y,1.0,.12));
        finalColor=mix(finalColor,gridColor,mesh*meshOpacity*visible*inside*smoothstep(2.5,4.5,borderDistance));
    }
    // Neutral outer frame and contrasting physical/PML interface; independent of grid opacity.
    float outerBorder = 1.0-smoothstep(1.0,2.0,outerDistance);
    finalColor = mix(finalColor,gridColor,outerBorder*0.85);
    if(npml>0.0) {
        float pmlBorder = 1.0-smoothstep(0.7,1.6,pmlDistance);
        vec3 borderColor = lightModel ? vec3(0.12,0.38,0.33) : vec3(0.55,0.88,0.75);
        finalColor = mix(finalColor,borderColor,pmlBorder*0.9);
    }
    FragColor = vec4(finalColor,1.0);
}
