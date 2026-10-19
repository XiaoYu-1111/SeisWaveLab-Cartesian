/**
 * @file      SeisWaveLab.cpp
 * @author    XIAO YU (1269466423@qq.com)
 * @brief     SeisWave Lab: CUDA elastic wave simulation, visualization and acquisition.
 * @date      2026-10-13
 *
 * @details
 * SeisWave Lab is an interactive 2D/3D scientific simulation and seismic data acquisition platform
 * developed with C++20, NVIDIA CUDA, OpenGL 4.3, and Dear ImGui/ImPlot. Version metadata comes from CMake.
 * It provides real-time, zero-copy physical wavefield propagation and industrial-grade seismic survey tools.
 *
 * Key System Capabilities & Architecture:
 * 1. Physical Governing Equations & Numerical Solvers:
 *    - First-order velocity-stress elastic wave equations.
 *    - Staggered-grid finite difference method (FDM): 8th-order spatial and 2nd-order temporal accuracy.
 *    - Boundary formulations: Whole-domain split PML, hybrid FDM+PML, and traction-free surface (antisymmetric imaging).
 *    - PML fluid-solid boundary stabilization: automatically solidifies fluid shear modulus (Vs = Vp / 2) to eliminate divergence.
 *    - Automated CFL stability analysis and safe time-step alignment (CFL = 0.480).
 *
 * 2. High-Performance Graphics & Interoperability:
 *    - CUDA-OpenGL Interoperability (zero-copy VRAM texture streaming) delivering 60+ FPS real-time visualization.
 *    - FBO off-screen rendering with aspect-ratio correction and pixel-distance adaptive physical scale rulers.
 *    - Dynamic runtime linking with NVML for real-time GPU core load, temperature, power, and VRAM telemetry.
 *
 * 3. Multi-Component Wavefield Separation & Adaptive Scaling:
 *    - Real-time extraction of 7 physical components: Velocity Mag, Vx, Vy (Z), Normal Stress, Shear Stress,
 *      pure P-wave (Divergence), and pure S-wave (Curl).
 *    - Cross-dimensional physical scaling: normalizes stress to strain (sigma / E) and applies wave speed compensation
 *      (v = Vp * e) for consistent dynamic range across all components.
 *
 * 4. Seismic Data Acquisition & Survey Sequencer:
 *    - Interactive linear geophone array deployment with GPU-side parallel trace gathering.
 *    - Active triggering (TRIGGER & ACQUIRE) and passive monitoring (START CAPTURE).
 *    - Automated multi-shot pre-stack gather sequencer with auto-resetting wavefields and coordinate stepping.
 *    - Standard SEG-Y dual-component (_vx.sgy, _vz.sgy) export embedding 240-byte trace headers with physical coordinates.
 *
 * 5. Interactive Sandbox & Wavefield Replay:
 *    - Real-time physical rock property paintbrushes (keys 1-5: Source, High-V, Low-V, Custom, Eraser) with vertical grid alignment.
 *    - 11 preset geophysical scenarios (Earth shell/core, double-slit, waveguides, phononic crystal, Penrose room, etc.).
 *    - CPU-RAM cached wavefield movie recording, binary export, and zero-copy timeline scrubbing playback.
 *    - Filename dimension auto-parsing for binary movie imports and exports.
 *
 * @copyright Copyright (c) 2025-2026 Xiao Yu. All rights reserved.
 * @license   Licensed under the MIT License.
 */

/**
 * @file    main.cpp
 * @brief   程序主入口：采用“Intro Screen + Simulation”双模式架构
 */

 // =================================================================================
 // 1. C/C++ 标准库 (Standard Libraries)
 // =================================================================================
#include <iostream>
#include <vector>
#include <deque>
#include <list>
#include <cmath>
#include <algorithm>
#include <complex>
#include <random>
#include <ctime>
#include <filesystem>     // C++17 跨平台路径处理

#pragma execution_character_set("utf-8")//设置中文字体utf-8

// =================================================================================
// 2. 图形基础库 (Graphics API & Window Management)
// =================================================================================
// 注意：GLEW 必须在 GLFW 之前包含，以确保正确的 OpenGL 函数指针加载
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// =================================================================================
// 3. 用户界面库 (User Interface Libraries)
// =================================================================================
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"       // ImGui 绘图扩展

// =================================================================================
// 4. 图像处理工具 (Image Utilities - stb)
// =================================================================================
// 警告：IMPLEMENTATION 宏只能在一个 .cpp 文件中定义，否则会导致符号重定义错误
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "stb_image.h"
#include "stb_image_write.h"

// =================================================================================
// 5. 本项目自定义头文件 (Project Specific Headers)
// =================================================================================
#include "SeisWaveLab.h" // 包含启动界面逻辑、系统结构体及核心计算单元
#include "AppInfo.h"
#include "../src/Seis3D/Internal3D.h"

// =================================================================================
// GLFW 错误捕获回调
// =================================================================================
static void glfw_error_callback(int error, const char* description) {
    std::cerr << "Glfw Error [" << error << "]: " << description << std::endl;
}

// =================================================================================
// 初始化离屏渲染四边形几何体 VAO/VBO
// =================================================================================
void initializeQuad(GLHandles& gl) {
    // 定义 4 个顶点：位置(x,y,z) + 纹理坐标(u,v)
    float vertices[] = {
        // 位置              // 纹理坐标
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  // 左上
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  // 左下
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // 右下

        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  // 左上
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // 右下
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f   // 右上
    };

    glGenVertexArrays(1, &gl.quadVAO);
    glGenBuffers(1, &gl.quadVBO);

    glBindVertexArray(gl.quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, gl.quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);

    // 位置属性 (Location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    // 纹理坐标属性 (Location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
}

// =================================================================================
// 主函数入口
// =================================================================================
// Save the actual application backbuffer at native resolution for documentation figures.
static void saveApplicationScreenshot(GLFWwindow* window,const char* filename) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    int frameWidth=0,frameHeight=0;glfwGetFramebufferSize(window,&frameWidth,&frameHeight);
    std::vector<unsigned char> pixels(std::size_t(frameWidth)*frameHeight*4);
    glReadPixels(0,0,frameWidth,frameHeight,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    stbi_flip_vertically_on_write(1);
    const std::string imageName(filename);
    auto folder=getExecutableDirectory().parent_path().parent_path()/"screenshots"/"application"/(imageName.rfind("seis2d",0)==0?"2d":"3d");
    if(const char* session=std::getenv("SEISWAVE_CAPTURE_SESSION")) {
        const std::string name(session);
        if(name.empty()||name.size()>80||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::invalid_argument("Invalid screenshot session name");
        folder/=name;
    }
    std::filesystem::create_directories(folder);
    const auto imagePath=pathToUtf8(folder/filename);
    if(!stbi_write_png(imagePath.c_str(),frameWidth,frameHeight,4,pixels.data(),frameWidth*4))
        throw std::runtime_error("Cannot save application screenshot: "+imagePath+" ("+std::to_string(frameWidth)+"x"+std::to_string(frameHeight)+")");
}

// Run normal interaction or an isolated screenshot session without saving user layout preferences.
#include "DocumentationCapture3D.h"

static int runApplication(bool displayCheck3D=false,bool paperCapture3D=false,bool chineseCapture3D=false,bool capture2D=false,bool workflowCapture3D=false,bool checkCapture2D=false) {
    // 启动前确认必要着色器可读，避免将空源码交给 OpenGL 并继续初始化。
    const std::string vsSrc = loadShaderFromFile(resolveResourcePath("shader/vs_code.vert"));
    const std::string fsSrc = loadShaderFromFile(resolveResourcePath("shader/fs_code.frag"));
    const std::string seisVsSrc = loadShaderFromFile(resolveResourcePath("shader/seis_code.vert"));
    const std::string seisFsSrc = loadShaderFromFile(resolveResourcePath("shader/seis_code.frag"));
    if (vsSrc.empty() || fsSrc.empty() || seisVsSrc.empty() || seisFsSrc.empty()) {
        std::cerr << "Startup failed: required shaders are missing or empty. "
            << "Deploy resource_CUDA_V1 beside the executable.\n";
        return -1;
    }
    // -----------------------------------------------------------------------------
    // Stage 1: CUDA 硬件环境诊断
    // -----------------------------------------------------------------------------
    // 设置控制台输出编码为UTF-8
    SetConsoleOutputCP(CP_UTF8);
    // 可选：控制台输入也设UTF8
    SetConsoleCP(CP_UTF8);
    std::cout << "========= CUDA 硬件环境测试 =========" << std::endl;
    GpuInfo info = getCudaDeviceInfo();
    if (info.success) {
        std::cout << "[成功] 找到 GPU 设备:" << std::endl;
        std::cout << "   - 设备名称: " << info.name << std::endl;
        std::cout << "   - 计算能力: " << info.computeCapabilityMajor << "." << info.computeCapabilityMinor << std::endl;
        std::cout << "   - 显存总量: " << (info.totalMem / 1024 / 1024) << " MB" << std::endl;
    }
    else {
        std::cerr << "CUDA unavailable; CPU / OpenMP simulation remains available." << std::endl;
        std::strcpy(info.name,"CPU / OpenMP");
    }

    // 系统与配置对象初始化
    SimState state;         // 状态管理
    SimConfig config;       // 程序配置
    if(capture2D) {config.winWidth=1600;config.winHeight=1000;}
    GLHandles gl;           // OpenGL 资源句柄
    AsyncLoader g_loader;   // 后台加载器
    Seis3D::Seis3D_State state3D;
    if(displayCheck3D) state3D.layoutPreset3D=0; // CPU-only until the 3-D sandbox is initialized.
    GLFWwindow* window = nullptr;
    // 正常退出和异常展开共用清理路径；各资源句柄均从空状态开始。
    struct ApplicationCleanup {
        GLHandles& handles;
        GLFWwindow*& window;
        Seis3D::Seis3D_State& sandbox3D;
        ~ApplicationCleanup() {
            Seis3D::releaseSandbox3D(sandbox3D);
            Seis2D::shutdownDeviceMonitor2D();
            cleanup(handles);
            if (ImGui::GetCurrentContext()) {
                if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
                if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
            }
            if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
            if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
            if (window) glfwDestroyWindow(window);
            glfwTerminate();
        }
    } applicationCleanup{gl, window, state3D};

    // -----------------------------------------------------------------------------
    // Stage 2: GLFW 与 OpenGL 窗口环境搭建
    // -----------------------------------------------------------------------------
    glfwSetErrorCallback(glfw_error_callback); // 注册错误回调

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // 设置 OpenGL 4.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 隐藏窗口直到 CUDA/OpenGL/ImGui 初始化完成并提交首帧，避免系统先显示白色未绘制客户区。
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    const auto windowTitle=AppUi::applicationWindowTitle();
    window = glfwCreateWindow(config.winWidth, config.winHeight, windowTitle.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    if(!capture2D||!checkCapture2D) glfwMaximizeWindow(window);
    glfwGetWindowSize(window, &config.winWidth, &config.winHeight);
    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, &gl); // 将 GL 句柄绑定到窗口以便后续回调

    // 加载窗口图标
    GLFWimage images[1];
    std::string image_icon = pathToUtf8(resolveResourcePath("images/icon_Seis.png"));
    images[0].pixels = stbi_load(image_icon.c_str(), &images[0].width, &images[0].height, 0, 4);
    if (images[0].pixels) {
        glfwSetWindowIcon(window, 1, images);
        stbi_image_free(images[0].pixels);
    }

    // 初始化 GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // -----------------------------------------------------------------------------
    // Stage 3: 离屏基础几何构建
    // -----------------------------------------------------------------------------
    initializeQuad(gl);

    // -----------------------------------------------------------------------------
    // Stage 4: 着色器与纹理管线编译
    // -----------------------------------------------------------------------------
    // 1. 加载渲染着色器 (可视化热力图等)
    gl.renderProg = createShaderProgram(vsSrc, fsSrc);
    gl.seisProg = createShaderProgram(seisVsSrc, seisFsSrc);

    // 可选演示资源在进入对应页面时才分配，主程序仅初始化地震工作区所需资源。
    Init_Imgui(window);
    Seis3D::Documentation3D::WorkflowCapture3D documentation3D;
    if(workflowCapture3D) {documentation3D.initialize3D(state3D);state.currentScreen=AppScreen::Intro0;}
    if(capture2D) {
        if(!checkCapture2D) ImGui::ClearIniSettings();
        ImGui::GetIO().IniFilename=nullptr;
        AppUi::setTheme(AppUi::ThemeId::PaperSand,false);AppUi::setLanguage(AppUi::Language::Chinese,false);
        Seis2D::setInterfaceAccent2D(AppUi::palette().accent);
        state.currentScreen=AppScreen::SeisSim_GPU;
        Seis2D::prepareDocumentation2D(gl,state);
    }
    if(displayCheck3D) {
        // Exercise real startup with all four views visible, without changing saved user preferences.
        ImGui::LoadIniSettingsFromDisk(pathToUtf8(getExecutableDirectory()/"imgui.ini").c_str());
        ImGui::GetIO().IniFilename=nullptr;
        if(paperCapture3D) AppUi::setTheme(AppUi::ThemeId::PaperSand,false);
        AppUi::setLanguage(chineseCapture3D?AppUi::Language::Chinese:AppUi::Language::English,false);
        state.currentScreen=paperCapture3D?AppScreen::Intro0:AppScreen::SeisSim_3D;
        // Documentation/check workloads remain explicit small fixtures rather than UI defaults.
        state3D.editContext3D.nx3D=state3D.editContext3D.ny3D=state3D.editContext3D.nz3D=96;
        state3D.survey3D.receiverCountX3D=state3D.survey3D.receiverCountY3D=10;
        state3D.targetSamples3D=256;state3D.stepsPerFrame3D=8;
        state3D.resetLayout3D=true;
        if(!paperCapture3D) {
            Seis3D::Detail3D::initializeSession3D(state3D,false);
            Seis3D::Detail3D::startGather3D(state3D,1);
        }
    }

    // 初始运行状态设定 (强制开启启动屏逻辑)
    state.isIntroMode = true;
    glfwSwapInterval(state.vsyncEnabled ? 1 : 0);
    bool firstFramePresented = false;
    AppScreen lastRenderedScreen = state.currentScreen;
    int displayCheckFrames3D=0;
    int documentationFrames2D=0;

    // =============================================================================
    // 主渲染循环
    // =============================================================================
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. 清空主后备缓冲区底色
        // 为窗口首帧和未覆盖区域提供与首页一致的深色底色。
        const auto& clearTheme = AppUi::palette();
        glClearColor(clearTheme.background.x,clearTheme.background.y,clearTheme.background.z,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 2. 动态垂直同步检测
        static bool lastVsync = state.vsyncEnabled;
        if (state.vsyncEnabled != lastVsync) {
            glfwSwapInterval(state.vsyncEnabled ? 1 : 0);
            lastVsync = state.vsyncEnabled;
        }

        // 3. 窗口大小采集与用户闲置状态追踪
        int winW, winH;
        glfwGetWindowSize(window, &winW, &winH);
        CheckIdleStatus(window, state);

        // 4. GUI 准备新帧数据
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if(capture2D||workflowCapture3D) ImGui::GetIO().AddMousePosEvent(-100,-100);
        if(capture2D&&(documentationFrames2D==20||documentationFrames2D==24)) ImGui::GetIO().AddKeyEvent(ImGuiKey_B,true);
        if(capture2D&&(documentationFrames2D==21||documentationFrames2D==25)) ImGui::GetIO().AddKeyEvent(ImGuiKey_B,false);
        ImGui::NewFrame();
        if(workflowCapture3D) documentation3D.beforeFrame3D(state3D,state);

        // 5. 事件响应与输入控制处理
        ProcessInput(window, config, state, gl);
        static auto lastUiTheme=AppUi::currentTheme();
        if(lastUiTheme!=AppUi::currentTheme()) {
            Seis2D::setInterfaceAccent2D(AppUi::palette().accent);lastUiTheme=AppUi::currentTheme();
        }

        // 6. 场景渲染分发 (包含启动页与核心模拟页面)
        // Previous-frame draw commands have completed before deleting their textures.
        // Release the old solver before the destination route can allocate any simulation fields.
        if (state.currentScreen != lastRenderedScreen) {
            if (lastRenderedScreen == AppScreen::SeisSim_GPU || lastRenderedScreen == AppScreen::SeisSim_3D)
                glFinish(); // Synchronize only at mode transitions, not in the normal simulation loop.
            if (lastRenderedScreen == AppScreen::SeisSim_GPU) releaseSeismicWorkspace2D(gl, state);
            if (lastRenderedScreen == AppScreen::SeisSim_3D) Seis3D::releaseSandbox3D(state3D);
        }
        lastRenderedScreen = state.currentScreen;
        RenderApp(state.currentScreen, state, info, winW, winH, gl, g_loader, state3D);
        state.isIntroMode = IsIntroScreen(state.currentScreen); // 同步页面标识

        // 7. GUI 指令集提交
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if(workflowCapture3D&&documentation3D.afterFrame3D(window,state3D)) return 0;
        if(capture2D) {
            ++documentationFrames2D;
            if(documentationFrames2D==22) {Seis2D::checkExternalBoundaryView2D(true);saveApplicationScreenshot(window,"seis2d-external-boundary.png");}
            if(documentationFrames2D==26) Seis2D::checkExternalBoundaryView2D(false);
            if(documentationFrames2D==28) saveApplicationScreenshot(window,"seis2d-wavefield-paper.png");
            if(documentationFrames2D==30) Seis2D::configureDocumentationGroups2D(0);
            if(documentationFrames2D==32) {Seis2D::configureDocumentationGroups2D(1);saveApplicationScreenshot(window,"seis2d-parameter-groups-folded.png");}
            if(documentationFrames2D==34) Seis2D::configureDocumentationGroups2D(2);
            if(documentationFrames2D==36) {Seis2D::configureDocumentationGroups2D(3);saveApplicationScreenshot(window,"seis2d-parameter-groups-expanded.png");}
            if(documentationFrames2D==50) {state.running=false;Seis2D::openCapturedRecord2D();Seis2D::selectDocumentationView2D(0);}
            if(documentationFrames2D==54) saveApplicationScreenshot(window,"seis2d-record-raw.png");
            if(documentationFrames2D==56) Seis2D::selectDocumentationView2D(1);
            if(documentationFrames2D==60) saveApplicationScreenshot(window,"seis2d-record-agc.png");
            if(documentationFrames2D==64) Seis2D::selectDocumentationView2D(2);
            if(documentationFrames2D==68) saveApplicationScreenshot(window,"seis2d-single-vz.png");
            if(documentationFrames2D==70) Seis2D::selectDocumentationView2D(3);
            if(documentationFrames2D==74) saveApplicationScreenshot(window,"seis2d-section-vx.png");
            if(documentationFrames2D==76) Seis2D::configureDocumentationWorkspace2D(0);
            if(documentationFrames2D==82) saveApplicationScreenshot(window,"seis2d-docking-analysis.png");
            if(documentationFrames2D==84) Seis2D::configureDocumentationWorkspace2D(1);
            if(documentationFrames2D==88) saveApplicationScreenshot(window,"seis2d-docking-record-focus.png");
            if(documentationFrames2D==90) Seis2D::configureDocumentationWorkspace2D(2);
            if(documentationFrames2D==96) saveApplicationScreenshot(window,"seis2d-export-package.png");
            if(documentationFrames2D>=100) {
                if(checkCapture2D) Seis2D::checkRecordingTimeline2D(state,gl);
                glfwSetWindowShouldClose(window,GLFW_TRUE);
            }
        }
        if(displayCheck3D) {
            if(state3D.messageIsError3D) throw std::runtime_error(state3D.message3D);
            ++displayCheckFrames3D;
            if(paperCapture3D&&state.currentScreen==AppScreen::Intro0&&displayCheckFrames3D==2) {
                saveApplicationScreenshot(window,(chineseCapture3D?"seis3d-chinese-home.png":"seis3d-paper-home.png"));
                Seis3D::Detail3D::applyVisualPreset3D(state3D,4);
                Seis3D::Detail3D::resetDisplayParameters3D(state3D);
                Seis3D::Detail3D::initializeSession3D(state3D,false);
                if(paperCapture3D) {state3D.sampleIntervalInMs3D=true;state3D.requestedSampleIntervalMs3D=.5;}
                Seis3D::Detail3D::startGather3D(state3D,1);
                state.currentScreen=AppScreen::SeisSim_3D;displayCheckFrames3D=0;
            }
            if(displayCheckFrames3D==20) {
                std::cout<<"[Seis3D display] ready "<<state3D.ready3D<<" rendered "<<state3D.gl3D.rendered3D<<" waveMax "<<state3D.viewMaximum3D<<" step "<<state3D.step3D<<'\n';
                for(int plane=0;plane<3;++plane) {
                    unsigned char pixel[4]{};
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,state3D.gl3D.framebuffers3D[plane]);
                    glReadPixels(state3D.gl3D.layout3D.sliceWidth3D[plane]/2,state3D.gl3D.layout3D.sliceHeight3D[plane]/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    std::cout<<"[Seis3D display] plane "<<plane<<" RGBA "<<int(pixel[0])<<','<<int(pixel[1])<<','<<int(pixel[2])<<','<<int(pixel[3])<<'\n';
                    if(pixel[3]!=255) throw std::runtime_error("Seis3D display target was not filled; offscreen rendering failed.");
                    bool submitted3D=false;
                    for(const auto* list3D:ImGui::GetDrawData()->CmdLists)
                        for(const auto& command3D:list3D->CmdBuffer)
                            if(command3D.GetTexID()==ImTextureID(state3D.gl3D.displayTextures3D[plane])&&command3D.ElemCount>=6)
                                submitted3D=true;
                    if(!submitted3D) throw std::runtime_error("Seis3D slice texture never reached the application UI.");
                }
                saveApplicationScreenshot(window,paperCapture3D?(chineseCapture3D?"seis3d-chinese-guide.png":"seis3d-paper-guide.png"):"seis3d-app-display-check.png");
            }
            if(paperCapture3D&&displayCheckFrames3D==24) ImGui::SetWindowFocus("Display / materials###Seis3D.Display");
            if(paperCapture3D&&displayCheckFrames3D==28) saveApplicationScreenshot(window,(chineseCapture3D?"seis3d-chinese-display.png":"seis3d-paper-display.png"));
            if(paperCapture3D&&displayCheckFrames3D==48) {
                state3D.layoutPreset3D=3;state3D.resetLayout3D=true;
                state3D.traceView3D=2;state3D.traceDisplay3D=2;state3D.traceRow3D=4;
            }
            if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL error during real application display check.");
            if(displayCheckFrames3D>=64) {
                if(state3D.gather3D.sampleCount3D!=256||state3D.gather3D.receivers3D.size()!=100)
                    throw std::runtime_error("Real application 100-channel capture did not complete.");
                if(paperCapture3D) saveApplicationScreenshot(window,(chineseCapture3D?"seis3d-chinese-receiver-row.png":"seis3d-paper-receiver-row.png"));
                std::cout<<"PASS: real application 96 cubed, opaque slice targets, 100-channel capture and full-size UI.\n";
                return 0;
            }
        }

        // 8. 多视口支持 (针对分离窗口渲染状态)
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_context);
        }

        glfwSwapBuffers(window);
        if (!firstFramePresented&&!displayCheck3D&&!capture2D) {
            // 首帧已在隐藏状态完成渲染并交换到前缓冲，再显示窗口。
            glfwShowWindow(window);
            firstFramePresented = true;
        }
    }

    // =============================================================================
    // 资源销毁与环境释放
    // =============================================================================
    return 0;
}

// 将输入验证和 CUDA 异常转换为明确诊断，避免失败后继续计算损坏的波场。
int main(int argc,char** argv) {
    try {
        const bool chineseCapture3D=argc==2&&std::string(argv[1])=="--capture-seis3d-chinese";
        const bool paperCapture3D=chineseCapture3D||(argc==2&&std::string(argv[1])=="--capture-seis3d-paper");
        const bool displayCheck3D=paperCapture3D||(argc==2&&std::string(argv[1])=="--check-seis3d-display");
        const bool checkCapture2D=argc==2&&std::string(argv[1])=="--capture-seis2d-paper";
        const bool capture2D=checkCapture2D||(argc==2&&std::string(argv[1])=="--capture-seis2d-workflow");
        const bool workflowCapture3D=argc==2&&std::string(argv[1])=="--capture-seis3d-workflow";
        return runApplication(displayCheck3D,paperCapture3D,chineseCapture3D,capture2D,workflowCapture3D,checkCapture2D);
    }
    catch (const std::exception& error) {
        std::cerr << "Application stopped safely: " << error.what() << std::endl;
        return 1;
    }
}
