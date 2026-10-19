# SeisWave Lab · Cartesian

二维 / 三维直角坐标弹性波模拟工作台。

<img src="resources/icons/SeisWaveLab.png" alt="SeisWave Lab Cartesian 程序图标" width="96" height="96">

[English overview](README.en.md) · [贡献说明](CONTRIBUTING.md) · v2.0.0

二维和三维笛卡尔规则网格各向同性弹性波模拟与交互式可视化工作台。主页左侧进入二维 X–Z 平面 P–SV 剖面，右侧进入三维直角体网格。两个维度有独立求解器、模型与记录会话，共用设备检测、主题、语言与地震记录显示模块。数值核心采用速度—应力交错网格有限差分，均支持 CPU/OpenMP 与 CUDA Float32；界面使用 Dear ImGui Docking、ImPlot 和 OpenGL 4.3。外观与 Curvilinear 项目采用同系列主题。

**完整说明**：[理论与完整流程](docs/PROJECT_MANUAL.md) · [二维操作指南](docs/user/Seis2D_User_Guide.md) · [三维操作指南](docs/user/Seis3D_User_Guide.md) · [代码架构](docs/development/CODE_ARCHITECTURE.md) · [构建说明](docs/build/BUILDING.md) · [文档导航](docs/README.md)

**性能实测**：[二维/三维 CPU 与 CUDA 对比报告、图表及原始数据](docs/performance/CPU_CUDA_REPORT.md)（2026-10-19，CPU 32 线程、六档模型、每种配置 5 轮；[复现步骤](benchmarks/README.md)）。

![二维正交剖面与三维直角体网格主页](docs/assets/seis3d/full/01-home.png)

二维提供模型编辑、单次/连续激发、外置 PML、Vx/Vz 接收测线、SEG-Y 物性模型导入和单炮数据包导出。物理模型 NX×NZ 不包含外加吸收层，按 B 显示/隐藏吸收区域。左侧控制面板与当前参数页签并列显示生效值和待应用值，提供更新、撤销及全部展开/折叠。左键激发，连续模式下按住左键追加源；中键/右键拖动平移，滚轮缩放。详见[二维工作流](docs/user/Seis2D_User_Guide.md)和[外置边界](docs/development/EXTERNAL_DOMAIN_2D.md)。

三维提供平面自由表面、五面/六面 CPML 与滑移反射边界、三正交剖面、体视图和 Vx/Vy/Vz 记录。默认物理网格 300×50×300、接收排列 100×5 共 500 道、每炮 1000 样点；默认体视图优先，控制、显示与当前参数集中在左侧，地震记录默认全部道。采集可按计算步距或等时间毫秒间隔设置。演示截图使用 200³、150 道独立算例，详细条件见[截图集](docs/user/Seis3D_Screenshot_Gallery.md)。

![二维波场与当前参数面板](docs/assets/seis2d/wavefield-paper.png)

## 模拟与显示

- 二维五场、三维九场各向同性弹性波；内部八阶交错空间差分与二阶时间推进，自由表面闭合单独处理。
- CPU/OpenMP 与 CUDA 可切换，线程数可配置；自动优先 CUDA，设备、驱动或互操作不可用时回退 CPU。切换计算核心需应用/重建会话。
- 分层介质、异常体和常见合成模型；二维物性导入统一使用 Vp、Vs、密度 SEG-Y 三文件组。三维支持 S3DM 模型读写及 VTI 模型/波场导出。
- 物理网格、外置吸收域、标尺和源检位置采用统一坐标；二维 B 键只切换显示，不改变吸收计算。三维剖面支持滚轮整体缩放和拖动平移。
- 独立波场/模型配色与色标，默认 Coolwarm 和 Scientific White；二维颜色增益默认 100、范围 0–1000。主题切换保留科学色带和已调显示参数。
- 地震记录支持全部道、局部道集、单道、RMS AGC、增益和独立缩放；导入和预览处理保留原始样本，SEG-Y 导出不应用显示增益。
- 中英文、系列主题、字号调整与停靠布局；参数按组展开/折叠，当前值与待应用值对照；性能面板显示计算耗时并提供 CPU/CUDA 对比。

![三维波场剖面、体视图与记录工作台](docs/assets/seis3d/full/08-wavefield.png)

当前项目采用规则网格和平面地表。曲面地表与贴体曲网格由独立的 Curvilinear 项目维护；两个项目的数值方法和边界实现各自独立。CPU/CUDA 一致性、有限算例的稳定性和物理精度属于不同验证范围，见[理论说明](docs/PROJECT_MANUAL.md)、[后端验证](docs/development/BACKEND_VALIDATION.md)与[性能测试条件](docs/performance/CPU_CUDA_REPORT.md)。

## 仓库与输出目录

**当前目录就是需要上传的 Git 仓库**，包含构建入口、源码、文档及其图片、图标、工具和许可。所有编译产物和运行生成文件统一写到本仓库同级的 `../out`：

```text
SeisWaveLab_Cartesian_Workspace/
├─ SeisWaveLab_Cartesian/       Git 仓库
│  ├─ CMakeLists.txt           独立构建入口
│  ├─ CMakeSettings.json       VS 配置
│  ├─ cmake/                  输出布局与编译依赖
│  ├─ src/SeisCommon/         共享入口、主页、主题、语言与 SEG-Y
│  ├─ src/Seis2D/             二维会话、CPU 与 cuda/ 求解
│  ├─ src/Seis3D/             三维会话、CPU 与 cuda/ 求解
│  ├─ include/                SeisCommon、Seis2D、Seis3D 公共头文件
│  ├─ resources/icons/        正式 PNG / ICO 图标
│  ├─ shaders/ resource_CUDA_V1/  原始运行资源
│  ├─ docs/                   理论、指南、截图与文档工具
│  │  ├─ assets/              当前截图与采集摘要
│  │  └─ performance/         正式性能报告、图表与原始数据
│  ├─ benchmarks/             实测入口、运行和报告脚本
│  ├─ tests/                  数值、记录与工作区回归
│  └─ LICENSE  THIRD_PARTY_NOTICES.md
└─ out/                       不属于仓库
   ├─ build/x64-Release/       外层 RelWithDebInfo 缓存
   ├─ build/x64-Debug/         外层 Debug 缓存
   ├─ build/standalone/        内部仓库独立构建缓存
   ├─ bin/RelWithDebInfo/      当前发布程序及运行资源
   ├─ bin/Debug/
   ├─ screenshots/            原生截图、日志与采集数据
   ├─ benchmarks/             本机测试输出
   ├─ documentation/          生成的离线说明包
   └─ blog/                   生成的 Hexo 发布包
```

文档图片、图标、正式性能结果和复现脚本随仓库维护；缓存、EXE/DLL、模拟导出和发布 ZIP 留在外层 `out`。目录职责及迁移清单见[仓库整理](docs/development/REPOSITORY_LAYOUT.md)和[代码架构](docs/development/CODE_ARCHITECTURE.md)。当前工作区为 `SeisWaveLab_Cartesian_Workspace`，Git 仓库为 `SeisWaveLab_Cartesian`，CMake 主程序目标保留 `SeisWaveLab`，唯一主程序文件名为 `SeisWaveLab_Cartesian.exe`。

## 构建与运行

需要 CMake 3.26+、MSVC x64、Ninja、C++20 与 CUDA Toolkit（本机验证为 12.8，CUDA 源码使用 C++17）。GLFW、GLEW、GLM 为外部 SDK，显示需要 OpenGL 4.3。运行时支持 CPU 回退，当前源码构建仍需要 CUDA Toolkit。以下命令从本 Git 仓库根目录、MSVC 开发者终端执行：

```powershell
cmake -S . -B ../out/build/standalone/RelWithDebInfo -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIB_ROOT=D:/CPP_lib
cmake --build ../out/build/standalone/RelWithDebInfo --target SeisWaveLab --parallel 4
..\out\bin\RelWithDebInfo\SeisWaveLab_Cartesian.exe
```

SDK 路径可通过 `SEISWAVE_GLM_ROOT`、`SEISWAVE_GLEW_ROOT`、`SEISWAVE_GLFW_ROOT` 单独配置。外层 VS 的 `x64-Release` 使用 `RelWithDebInfo`，缓存为 `out/build/x64-Release`；内部独立构建使用 `out/build/standalone/<配置>`，两个入口不能混用缓存。具体配置、依赖和回归命令见[构建说明](docs/build/BUILDING.md)。

## 文档、博客与资源

本轮于 **2026-10-19** 从当前程序重新采集 **33 张三维 + 11 张二维**原生 **2560×1494** 完整窗口截图，包含主页、参数、外置边界、波场、记录、AGC、导入导出和分组折叠。图片与采集日期、SHA-256、程序摘要均在 `docs/assets` 内，GitHub 阅读不依赖本机 `out`。截图条件与普通默认值分别记录，波场和记录来自真实 CUDA 计算。

从外层工作区运行截图命令，再从内部仓库发布文档（Python 工具需要 Pillow）：

```powershell
# 外层工作区
.\out\bin\RelWithDebInfo\SeisWaveLab_Cartesian.exe --capture-seis3d-workflow
.\out\bin\RelWithDebInfo\SeisWaveLab_Cartesian.exe --capture-seis2d-workflow
# 内部 Git 仓库
python -B docs/tools/prepare_paper_screenshots.py
python -B docs/tools/prepare_seis2d_screenshots.py
python -B docs/tools/package_documentation.py
```

维护源为 `docs/PROJECT_MANUAL.md`、分类用户指南与架构说明。离线包生成到 `../out/documentation`，Hexo 文章和同名资源目录生成到 `../out/blog`；数学排版设置见[Hexo 说明](docs/build/HEXO_MATH.md)。本轮截图与文档检查见[更新记录](docs/reviews/DOCUMENTATION_REFRESH_2026_10_19.md)。

图标原件为 `resources/icons/SeisWaveLab.png`、`SeisWaveLab.ico`，ICO 编入 Windows EXE。运行/分发时保留 EXE 同级 DLL、`resource_CUDA_V1`、`resource_Seis3D` 及许可文件。正式模型/记录格式见 [S3DM](docs/user/S3DM_Model_Format.md)、[VTK](docs/user/VTK_Model_Export.md)和[二维数据包](docs/user/Seis2D_User_Guide.md)。

## 操作与来源

二维画布滚轮缩放、中键/右键拖动平移、左键激发，连续激发需打开对应开关；B 显示/隐藏外置吸收层。三维剖面滚轮缩放、拖动平移，体视图提供独立旋转/缩放/平移操作，F1 查看快捷键。模式切换和参数重建会结束原会话，采集前先核对生效参数；显示增益与 AGC 不改变导出振幅。

理论、实现及验证范围见[完整说明](docs/PROJECT_MANUAL.md)。作者 XIAO YU；许可见 [LICENSE](LICENSE)，依赖和资源来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
