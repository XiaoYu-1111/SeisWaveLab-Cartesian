# ==========================================
# 1. 基础环境配置 (必须在 project 之前)
# ==========================================
cmake_minimum_required(VERSION 3.26) # native CUDA architectures and resource synchronization

# 直接在 VS 中打开本目录时，也使用外层 out 存放编译产物。
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BuildLayout.cmake")

# [核心修复] 针对 MSVC + Ninja 环境下的 NVCC 路径转义 Bug
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded) # 强制 /Z7 嵌入调试信息
set(CMAKE_CUDA_COMPILE_OPTIONS_MSVC_PDB_OUTPUT_PATH "" CACHE STRING "" FORCE) # 彻底禁用 -Fd 参数

# Respect a caller-selected architecture list; local builds default to the installed GPU.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES native CACHE STRING "CUDA architectures for SeisWave Lab")
endif()

# [关键修改] 显式声明支持 CUDA 语言
project(SeisWaveLab_Cartesian VERSION 2.0.0 LANGUAGES CXX C CUDA)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/MsvcDependencies.cmake")
# Use the native SDK finder; CUDA language detection alone does not create CUDA:: library targets.
find_package(CUDAToolkit REQUIRED MODULE)
if(NOT TARGET CUDA::cudart OR NOT TARGET CUDA::cuda_driver)
    # Some locally edited toolchains intercept find_package without forwarding it.
    # Recover through the installed CMake module, without modifying the user's shared toolchain.
    message(WARNING "The toolchain did not create the CUDA library targets. Loading CMake's native FindCUDAToolkit module directly.")
    set(CUDAToolkit_FIND_REQUIRED TRUE)
    include("${CMAKE_ROOT}/Modules/FindCUDAToolkit.cmake")
    unset(CUDAToolkit_FIND_REQUIRED)
endif()
if(NOT TARGET CUDA::cudart OR NOT TARGET CUDA::cuda_driver)
    message(FATAL_ERROR "CUDA Toolkit discovery did not provide CUDA::cudart / CUDA::cuda_driver. Check CUDAToolkit_ROOT and the selected toolchain.")
endif()
option(SEISSIM_BUILD_SOLVER_TESTS "Build 2D record and CUDA solver regression checks" OFF)
enable_testing()

set(TARGET_NAME "SeisWaveLab")

# Public product metadata has one version source; generated files stay in the outer build tree.
configure_file("Main_header/AppInfo.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/AppInfo.h" @ONLY)
if(WIN32)
    enable_language(RC)
    configure_file("resources/SeisWaveLab.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/SeisWaveLab.rc" @ONLY)
endif()

# ==========================================
# 2. External dependencies (caller-configurable, legacy layout retained as a default)
# ==========================================
set(LIB_ROOT "D:/CPP_lib" CACHE PATH "Root containing the external OpenGL dependencies")
set(OPENGL_ROOT "${LIB_ROOT}/OpenGL")
set(SEISWAVE_GLM_ROOT "${OPENGL_ROOT}/glm-master/glm-master" CACHE PATH "Directory containing glm/glm.hpp")
set(SEISWAVE_GLEW_ROOT "${OPENGL_ROOT}/glew-2.1.0-win32/glew-2.1.0" CACHE PATH "GLEW Windows SDK directory")
set(SEISWAVE_GLFW_ROOT "${OPENGL_ROOT}/glfw-3.4.bin.WIN64/glfw-3.4.bin.WIN64" CACHE PATH "GLFW Windows SDK directory")
foreach(seisWaveHeader IN ITEMS "${SEISWAVE_GLM_ROOT}/glm/glm.hpp" "${SEISWAVE_GLEW_ROOT}/include/GL/glew.h" "${SEISWAVE_GLFW_ROOT}/include/GLFW/glfw3.h")
    if(NOT EXISTS "${seisWaveHeader}")
        message(FATAL_ERROR "Missing dependency: ${seisWaveHeader}. Set LIB_ROOT or SEISWAVE_GLM_ROOT / SEISWAVE_GLEW_ROOT / SEISWAVE_GLFW_ROOT.")
    endif()
endforeach()

# ==========================================
# 3. 收集源文件
# ==========================================
# ImGui & ImPlot (Docking 分支)
file(GLOB IMGUIDocking_SRCS
    "ImGui_docking/*.cpp"
    "ImPlot/*.cpp"
)

# CUDA 核函数文件
file(GLOB CUDA_USER_SOURCES "CUDA_Kernels/Cuda_Check.cu")
file(GLOB CUDA_USER_HEADERS "CUDA_Kernels/Cuda_Check.cuh")

# 项目主源文件
set(PROJECT_SRCS "Main_cpp/SeisWaveLab.cpp" "Main_cpp/DocumentationCapture3D.h")

# STB Image (仅头文件)
set(STB_SRCS "stb_image/stb_image_write.h")

# ==========================================
# 4. 创建可执行文件
# ==========================================
add_executable(${TARGET_NAME}
    ${IMGUIDocking_SRCS}
    ${STB_SRCS}
    ${PROJECT_SRCS} 
    ${CUDA_USER_SOURCES}
    ${CUDA_USER_HEADERS}
    "Main_header/Common.h" "Main_header/AppShortcuts.h"
    "Main_header/ResourcePaths.h"
    "Main_cpp/ResourcePaths.cpp"
    "Main_cpp/SeismicIO.cpp"
    "Main_header/TraceProcessing.h" "Main_header/SeismicViewer2D.h" "Main_header/WavePalettes.h" "Main_header/RecordVisuals.h"
    "src/Seis2D/Simulation2D.cpp" "src/Seis2D/RecordData2D.cpp" "src/Seis2D/RecordViewer2D.cpp" "src/Seis2D/ShotExport2D.cpp" "Main_header/ShotExport2D.h"
    "src/Seis2D/State2D.inc" "src/Seis2D/Material2D.inc" "src/Seis2D/Canvas2D.inc" "src/Seis2D/Numerics2D.inc" "src/Seis2D/ModelPresets2D.inc" "src/Seis2D/ModelIo2D.inc"
    "src/Seis2D/Acquisition2D.inc" "src/Seis2D/Workflow2D.inc" "src/Seis2D/Workspace2D.inc" "src/Seis2D/DockWorkspace2D.inc" "src/Seis2D/ExportPanel2D.inc")
source_group("Seis2D\\Core" FILES "src/Seis2D/Simulation2D.cpp" "src/Seis2D/State2D.inc" "src/Seis2D/Numerics2D.inc" "src/Seis2D/Material2D.inc" "src/Seis2D/ModelPresets2D.inc" "src/Seis2D/Workflow2D.inc")
source_group("Seis2D\\IO" FILES "src/Seis2D/ModelIo2D.inc" "src/Seis2D/Acquisition2D.inc" "src/Seis2D/RecordData2D.cpp" "src/Seis2D/ShotExport2D.cpp" "src/Seis2D/ExportPanel2D.inc")
source_group("Seis2D\\UI" FILES "src/Seis2D/Workspace2D.inc" "src/Seis2D/Canvas2D.inc" "src/Seis2D/RecordViewer2D.cpp" "Main_header/SeismicViewer2D.h" "src/Seis2D/DockWorkspace2D.inc")
if(WIN32)
    target_sources(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/SeisWaveLab.rc")
    target_link_libraries(${TARGET_NAME} PRIVATE bcrypt.lib ole32.lib uuid.lib comdlg32.lib)
endif()

# ==========================================
# 5. 编译选项与标准设置
# ==========================================
set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME "SeisWaveLab"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CUDA_STANDARD 17
    CUDA_RUNTIME_LIBRARY Shared
    MSVC_DEBUG_INFORMATION_FORMAT Embedded
)

#解决cuda链接出错问题
if (MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/FS>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    )
endif()

# 针对不同语言的特定编译选项
target_compile_options(${TARGET_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --use_fast_math
        -Xcompiler=/wd4819
        -Xcompiler=/utf-8
        -Xcompiler=/Z7
    >
)

# ==========================================
# 6. 资源文件同步 (resource_CUDA_V1)
# ==========================================
set(RESOURCE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resource_CUDA_V1")
set(RESOURCE_DEST_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>/resource_CUDA_V1")

file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS "${RESOURCE_SRC_DIR}/*")

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${RESOURCE_SRC_DIR}" "${RESOURCE_DEST_DIR}"
    COMMENT "Syncing resource files..."
    VERBATIM
)
# 修改字体或着色器时也重新执行链接后的部署步骤。
set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS ${RESOURCE_FILES})

# ==========================================
# 7. 配置头文件路径 (Include)
# ==========================================
target_include_directories(${TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Main_header"
    "${CMAKE_CURRENT_SOURCE_DIR}/ImGui_docking"
    "${CMAKE_CURRENT_SOURCE_DIR}/ImPlot"
    "${CMAKE_CURRENT_SOURCE_DIR}/stb_image"
    "${CMAKE_CURRENT_SOURCE_DIR}/CUDA_Kernels"
    
    # 第三方库
    "${SEISWAVE_GLM_ROOT}"
    "${SEISWAVE_GLEW_ROOT}/include"
    "${SEISWAVE_GLFW_ROOT}/include"
    ${CUDAToolkit_INCLUDE_DIRS}
)

# ==========================================
# 8. 配置库路径 (Link Directories)
# ==========================================
target_link_directories(${TARGET_NAME} PRIVATE
    "${SEISWAVE_GLFW_ROOT}/lib-vc2022"
    "${SEISWAVE_GLEW_ROOT}/lib/Release/x64"
)

# ==========================================
# 9. 链接库与定义
# ==========================================
target_compile_definitions(${TARGET_NAME} PRIVATE GLEW_STATIC)

target_link_libraries(${TARGET_NAME} PRIVATE
    # OpenGL
    glfw3.lib
    glew32s.lib
    opengl32.lib
    user32.lib
    gdi32.lib
    shell32.lib
    
    # SDL2
    #SDL2.lib
    #SDL2_image.lib
    #SDL2_mixer.lib

    # CUDA (显式路径下的库名)
    CUDA::cudart
    CUDA::cuda_driver
)

# ==========================================
# 10. IDE 视图美化 (可选)
# ==========================================
source_group("Source Files\\CUDA" FILES ${CUDA_USER_SOURCES} ${CUDA_USER_HEADERS})
source_group("Source Files\\ImGui" FILES ${IMGUIDocking_SRCS})

# 三维求解与渲染拥有独立编译目标，不复用二维上下文或快速数学编译选项。
add_library(Seis3D STATIC
    "include/Common3D.h" "include/Seis3D_Sim.h"
    "cuda/Cuda_Seis3D.cuh" "cuda/Cuda_Seis3D.cu" "src/Seis3D_Sim.cpp"
    "src/Seis3D/Renderer3D.cpp" "src/Seis3D/Workspace3D.cpp" "src/Seis3D/SliceView3D.cpp" "src/Seis3D/TraceView3D.cpp"
    "src/Seis3D/ModelIo3D.cpp" "src/Seis3D/VtiExport3D.cpp" "src/Seis3D/Acquisition3D.cpp" "src/Seis3D/ModelPresets3D.cpp" "src/Seis3D/ShotRecordIo3D.cpp" "src/Seis3D/FileDialogs3D.cpp"
    "src/Seis3D/Internal3D.h" "src/Seis3D/Visual3D.h")
get_target_property(SEIS3D_SHARED_INCLUDE_DIRS ${TARGET_NAME} INCLUDE_DIRECTORIES)
get_target_property(SEIS3D_SHARED_LINK_DIRS ${TARGET_NAME} LINK_DIRECTORIES)
add_library(AppUi STATIC "Main_header/AppTheme.h" "Main_cpp/AppTheme.cpp"
    "Main_header/AppLanguage.h" "Main_header/AppWidgets.h" "Main_cpp/AppLanguage.cpp" "Main_cpp/AppTranslations.inc"
    "Main_header/HomeScreen.h" "Main_cpp/HomeScreen.cpp" "Main_cpp/OperationManual.cpp" "Main_header/AppInfo.h.in")
target_include_directories(AppUi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Main_header" "${CMAKE_CURRENT_BINARY_DIR}/generated" PRIVATE ${SEIS3D_SHARED_INCLUDE_DIRS})
target_compile_definitions(AppUi PRIVATE GLEW_STATIC)
set_target_properties(AppUi PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON MSVC_DEBUG_INFORMATION_FORMAT Embedded)
if(MSVC)
    target_compile_options(AppUi PRIVATE /utf-8)
endif()
target_include_directories(Seis3D PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/cuda")
target_include_directories(Seis3D PRIVATE ${SEIS3D_SHARED_INCLUDE_DIRS})
target_link_directories(Seis3D PRIVATE ${SEIS3D_SHARED_LINK_DIRS})
target_compile_definitions(Seis3D PRIVATE GLEW_STATIC)
set_target_properties(Seis3D PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    CUDA_STANDARD 17 CUDA_RUNTIME_LIBRARY Shared MSVC_DEBUG_INFORMATION_FORMAT Embedded)
if(MSVC)
    target_compile_options(Seis3D PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>)
endif()
target_link_libraries(Seis3D PRIVATE AppUi CUDA::cudart glew32s.lib opengl32.lib)
if(WIN32)
    target_link_libraries(Seis3D PRIVATE comdlg32.lib ole32.lib uuid.lib shell32.lib)
endif()
target_link_libraries(${TARGET_NAME} PRIVATE AppUi Seis3D)
source_group("Seis3D\\Workspace" FILES "src/Seis3D/Workspace3D.cpp" "src/Seis3D/SliceView3D.cpp" "src/Seis3D/TraceView3D.cpp" "src/Seis3D/Visual3D.h")
source_group("Seis3D\\Rendering" FILES "src/Seis3D/Renderer3D.cpp")
source_group("Seis3D\\Models" FILES "src/Seis3D/ModelPresets3D.cpp")
source_group("Seis3D\\IO" FILES "src/Seis3D/ModelIo3D.cpp" "src/Seis3D/VtiExport3D.cpp" "src/Seis3D/ShotRecordIo3D.cpp" "src/Seis3D/FileDialogs3D.cpp")
source_group("Seis3D\\Data IO" FILES "src/Seis3D/ModelIo3D.cpp" "src/Seis3D/Acquisition3D.cpp")
file(GLOB SEIS3D_SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*")
set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS ${SEIS3D_SHADER_FILES})
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/resource_Seis3D"
    COMMENT "Deploying independent Seis3D shaders..." VERBATIM)

# Optional focused graphics regression; runs without displaying an application window.
option(SEIS3D_BUILD_BOUNDARY_CHECKS "Build headless CUDA boundary diagnostics" OFF)
if(SEIS3D_BUILD_BOUNDARY_CHECKS)
    add_executable(Seis3DBoundaryRegression "tests/Seis3DBoundaryRegression.cpp")
    target_include_directories(Seis3DBoundaryRegression PRIVATE ${SEIS3D_SHARED_INCLUDE_DIRS})
    target_link_directories(Seis3DBoundaryRegression PRIVATE ${SEIS3D_SHARED_LINK_DIRS})
    target_link_libraries(Seis3DBoundaryRegression PRIVATE Seis3D CUDA::cudart)
    set_target_properties(Seis3DBoundaryRegression PROPERTIES CXX_STANDARD 20 MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    add_test(NAME Seis3DBoundaryRegression COMMAND Seis3DBoundaryRegression)
    set_tests_properties(Seis3DBoundaryRegression PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240)
endif()

option(SEIS3D_BUILD_WORKSPACE_CHECKS "Build CUDA/GL workspace regression" OFF)
if(SEIS3D_BUILD_WORKSPACE_CHECKS)
    add_executable(Seis3DWorkspaceRegression "tests/Seis3DWorkspaceRegression.cpp"
        "Main_cpp/ResourcePaths.cpp" ${IMGUIDocking_SRCS})
    target_include_directories(Seis3DWorkspaceRegression PRIVATE ${SEIS3D_SHARED_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_directories(Seis3DWorkspaceRegression PRIVATE ${SEIS3D_SHARED_LINK_DIRS})
    target_compile_definitions(Seis3DWorkspaceRegression PRIVATE GLEW_STATIC)
    set_target_properties(Seis3DWorkspaceRegression PROPERTIES CXX_STANDARD 20 MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    if(MSVC)
        target_compile_options(Seis3DWorkspaceRegression PRIVATE /utf-8)
    endif()
    target_link_libraries(Seis3DWorkspaceRegression PRIVATE Seis3D glfw3.lib glew32s.lib opengl32.lib CUDA::cudart user32.lib gdi32.lib shell32.lib)
    add_dependencies(Seis3DWorkspaceRegression ${TARGET_NAME})
    add_test(NAME Seis3DWorkspaceRegression COMMAND Seis3DWorkspaceRegression)
    set_tests_properties(Seis3DWorkspaceRegression PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
endif()

# 无图形窗口的二维数值回归程序，直接调用生产 CUDA 求解器。
if(SEISSIM_BUILD_SOLVER_TESTS)
    add_executable(Seis2DRecordRegression "tests/Seis2DRecordRegression.cpp" "src/Seis2D/RecordData2D.cpp" "src/Seis2D/ShotExport2D.cpp" "Main_cpp/SeismicIO.cpp")
    if(WIN32)
        target_link_libraries(Seis2DRecordRegression PRIVATE bcrypt.lib)
    endif()
    target_include_directories(Seis2DRecordRegression PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Main_header")
    set_target_properties(Seis2DRecordRegression PROPERTIES CXX_STANDARD 20 MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    if(MSVC)
        target_compile_options(Seis2DRecordRegression PRIVATE /utf-8)
    endif()
    add_test(NAME Seis2DRecordRegression COMMAND Seis2DRecordRegression)
    add_executable(SeisSimSolverRegression "tests/SolverRegression.cpp" ${CUDA_USER_SOURCES})
    get_target_property(SOLVER_TEST_INCLUDES ${TARGET_NAME} INCLUDE_DIRECTORIES)
    get_target_property(SOLVER_TEST_LINK_DIRS ${TARGET_NAME} LINK_DIRECTORIES)
    get_target_property(SOLVER_TEST_OPTIONS ${TARGET_NAME} COMPILE_OPTIONS)
    target_include_directories(SeisSimSolverRegression PRIVATE ${SOLVER_TEST_INCLUDES})
    target_link_directories(SeisSimSolverRegression PRIVATE ${SOLVER_TEST_LINK_DIRS})
    target_compile_options(SeisSimSolverRegression PRIVATE ${SOLVER_TEST_OPTIONS})
    target_compile_definitions(SeisSimSolverRegression PRIVATE GLEW_STATIC)
    set_target_properties(SeisSimSolverRegression PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CUDA_STANDARD 17 MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    target_link_libraries(SeisSimSolverRegression PRIVATE CUDA::cudart)
    add_test(NAME SolverRegression COMMAND SeisSimSolverRegression)
    set_tests_properties(SolverRegression PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()
