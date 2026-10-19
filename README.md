# SeisWave Lab

[English overview](README.en.md) · [贡献说明](CONTRIBUTING.md) · [现有 MIT 许可证](LICENSE)

基于 CPU/OpenMP、CUDA、OpenGL 4.3、Dear ImGui Docking 和 ImPlot 的二维与三维弹性波模拟工作区。首页分别进入二维波场沙盒与 Seis3D 三维沙盒，切换模式时释放原模式的计算和图形资源。

软件名称为 **SeisWave Lab**，当前版本 **v2.0.0**，窗口标题为 **SeisWave Lab — Cartesian Elastic Wave Simulation | v2.0.0**。三维模块与命名空间继续使用 **Seis3D**。主页、操作手册的“关于 SeisWave Lab”和 Windows 文件属性显示相同的版本信息；后续版本号统一在本仓库 `CMakeLists.txt` 的 `project(... VERSION ...)` 中修改。

本目录是 Git 仓库根目录。源码、原始运行资源、Markdown 文档、说明书截图与文档导出工具集中在本仓库，VS 的 Git 更改窗口可以识别这些文件。

## 文档入口

- [完整理论与流程说明](docs/PROJECT_MANUAL.md)：笛卡尔网格方程、19 组编号公式、CUDA 实现、完整操作与复现条件。
- [仓库分类](docs/development/REPOSITORY_LAYOUT.md)与[Hexo 数学排版](docs/build/HEXO_MATH.md)：当前截图、历史图片及发布目录约定。
- [二维模拟与地震记录图文指南](docs/user/Seis2D_User_Guide.md)：模型到采集全流程、三种停靠布局、单/多道、AGC、增益保持、单炮数据包，含八张实际 CUDA 截图。
- [论文准备与期刊建议](docs/publication/PUBLICATION_ROADMAP.md)：定量验证矩阵、性能实验、开源发布与科研软件投稿路线。
- [二维代码整理与后续优化](docs/development/SEIS2D_REFACTOR_AND_VIEWER.md)：文件职责、生命周期、采集问题修复、回归结果与分阶段建议。
- [三维模拟图文使用说明](docs/user/Seis3D_User_Guide.md)：Paper Sand 界面、浅色模型、蓝红波场、默认 100×5 接收排列与 500 道上限。
- [S3DM 模型格式与读取代码](docs/user/S3DM_Model_Format.md)：自定义模型文件的字节布局、单位、版本兼容，以及不依赖 GPU 的 Python/C++ 读取示例。
- [VTK ImageData 模型与波场导出](docs/user/VTK_Model_Export.md)：导出完整物理网格、Vp/Vs/密度或当前分量波场快照，供外部 VTK/ParaView 使用。
- [三维实操视频教程](docs/user/Seis3D_Video_Tutorial.md)：模型、震源、100 道采集、AGC 显示与 SEG-Y 导出。
- [三维项目展示文章](docs/showcase/CUDA_Seis3D_GitHub_Pages.md)：适合个人博客展示的完整介绍，包含计算原理与实际运行截图。
- [文档导航](docs/README.md)：操作、构建、实现与历史审查资料。
- [代码架构](docs/development/CODE_ARCHITECTURE.md)与[后端验证](docs/development/BACKEND_VALIDATION.md)：Seis2D / Seis3D / SeisCommon 分类、CPU/OpenMP 与 CUDA、参数草稿及分组折叠。
- [系列主题](docs/development/THEME_SERIES.md)与[二维外置吸收边界](docs/development/EXTERNAL_DOMAIN_2D.md)：主题、物理域及画布交互约定。
- [32 线程 CPU/CUDA 性能报告](docs/performance/CPU_CUDA_REPORT.md)：六档模型、5 轮中位数、波场误差、图表和原始数据；[复现步骤](benchmarks/README.md)。
- [构建与输出位置](docs/build/BUILDING.md)：VS 与命令行统一使用外层 `out`。
- [三维实现说明](docs/development/SEIS3D_IMPLEMENTATION.md)：求解、显存、I/O 与当前验证边界。
- [原二维 README](docs/history/Seis2D_Original_README.md)：保留旧版本介绍、界面与当时的性能描述。

可编辑 Word 由图文指南和截图导出，保存在外层 `out/docs/Seis3D_User_Guide.docx`。导出方式见文档导航。

2026-10-18 已重新采集二维 8 张、三维 33 张 2560×1494 完整窗口图。当前项目仅维护笛卡尔网格与平面地表；独立曲网格项目分别维护。运行 `python -B docs/tools/package_documentation.py` 可生成父目录 `out/documentation/SeisWaveLab` 和 `out/blog/SeisWaveLab_Hexo.zip`；主文与截图保存在本仓库，生成文件不加入 Git。

## 启动与显示

二维实现已整理到 `src/Seis2D`，`Seis_Sim.h` 保留公共入口。新的地震记录查看器可打开当前 Vz/Vx 采集快照，也可独立导入单炮 SEG-Y；提供单道/多道、滑动 RMS AGC、独立增益和显式显示重置。切换分量、导入和刷新保留显示设置，正式导出保持原始振幅。首页操作手册新增“二维模拟与地震记录”章节。

发布程序位于父目录下的 `out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`，Debug 位于 `out/bin/Debug/SeisWaveLab_Cartesian.exe`。CMake 项目为 `SeisWaveLab_Cartesian`，主程序目标为 `SeisWaveLab`。运行时保留程序旁的 `resource_CUDA_V1` 和 `resource_Seis3D`。

2026-10-18 完成项目重命名：外层工作区为 `SeisWaveLab_Cartesian_Workspace`，本 Git 仓库目录为 `SeisWaveLab_Cartesian`。在 VS 重新打开新目录；旧路径的 CMake/IDE 缓存不能直接复用。迁移说明见 [项目命名](docs/development/PROJECT_NAMING.md)。软件版本仍为 v2.0.0，Seis2D/Seis3D 模块名和 Git 历史保持。

三维初始为 **Vz / Paper scientific（论文浅色）/ Coolwarm**，使用 Scientific White 浅色材料和 1.8 倍波场显示增益，历史峰值保持关闭。默认网格 **300×50×300**，接收排列 **X 向每排 100 道、Y 向 5 排，共 500 道**，每炮 **1000 样点**，接收总上限 **500 道**；初始为单炮激发，多炮布设默认为 **1×1**。网格预设中的 96³/160³ 已替换为 **500×20×500 / 200³**。主频应用、重发或新采集同步更新子波与 CPML，保留模型和显示增益；参数生效规则见三维指南。界面主题独立选择，可在三维页设置 **Interface theme → Paper Sand**，搭配默认论文浅色预设。

首页设备摘要旁提供双语言 **Operating manual / 操作手册**。模型文件支持独立输入与输出路径选择；采集支持导出文件夹、中文文件名前缀和可选会话子文件夹，已有目标不会被覆盖。

界面提供十套主题，新增浅色 **Arctic Lab / Warm Ivory**，计算、模型、震源、采集和显示采用统一功能颜色。体视图默认开启模型尺寸透明度补偿，并提供独立 **Volume gain** 和三种 **Volume recipe**；可增强大模型波前显示，不改变求解或采集振幅。操作和实际截图见图文使用说明。

Visual preset 仅改变配色，初始化/重建/导入和重发震源保留显示增益等参数；**Lock current amplitude scale** 可固定跨运行比较的幅值参考。**Receiver focus** 放大道集，支持全部道、固定 Y 的 X 排、固定 X 的 Y 排及原单点三分量图；独立 Trace gain 与选行只影响预览。检查记录见 [显示状态与接收选行](docs/reviews/SEIS3D_DISPLAY_STATE_RECEIVER_REVIEW.md)。

Receiver traces 提供可调的滑动 RMS 自动增益补偿（AGC）：时间窗、补偿强度、最大放大量和噪声门限均可独立设置。AGC 在显示抽稀前处理预览，正式采样和 SEG-Y 导出保持原始振幅。操作说明见图文指南第 8 节。

Vx/Vy/Vz/速度幅值切换共用速度参考，并记忆独立的压力尺度。**Acquisition → IMPORT SHOT RECORD** 可导入已有单炮 SEG-Y，在 **Imported** 数据源中按单道、多道和 X/Y 排查看；导入记录与当前模拟/采集状态独立。操作步骤见图文指南，验证条件见 [分量尺度与单炮导入检查](docs/reviews/SEIS3D_COMPONENT_SCALE_SHOT_IMPORT_REVIEW.md)。

首页提供 **English / 简体中文** 切换，三维也可从 **Workspace → Language** 进入；语言偏好保存至程序旁的 `ui_language.ini`。三维操作面板采用默认收起的详细参数组，提供 **Expand all / Collapse all**，保留常用运行与采集操作。切换语言或折叠分组保留参数与三维停靠布局。截图与验证见 [双语界面与折叠面板检查](docs/reviews/SEIS3D_BILINGUAL_COMPACT_UI_REVIEW.md)。

三维 Simulation 现在提供九种常见合成模型，并可选择自由表面加五面吸收、六面吸收、六面滑移全反射。模型与边界改动在重建会话后生效。Workspace / Display 中的 **Show boundary layers / walls** 可即时显示实际 CPML 区域；隐藏显示仍保持吸收计算。默认字号约 21 像素，支持工作区字号缩放，已有布局可通过 Workspace 重新应用预设。

物理网格按总量限制为 **300 × 300 × 400（3600 万节点）**，单边不再固定限制为 512，同时保留 4 GiB 预算和硬件限制。体视图使用每轴最多 256 点、匹配视口的输出（最长边 1536 像素）和自适应射线采样。可开启峰值保持，避免自动色阶放大晚期残留；当前默认关闭。显示 CPML 时，吸收层参与共同振幅统计。数值对照条件与结果见 [体绘制与六面吸收检查](docs/reviews/SEIS3D_VOLUME_BOUNDARY_REVIEW.md)。

## 目录约定

`docs/user` 保存操作说明，`docs/assets` 保存实际截图，`docs/tools` 保存导出工具；构建说明、实现说明和历史资料也按类别存放于 `docs`。

本仓库包含独立构建所需的 CMake 辅助文件；外层工作区入口继续兼容。直接在 VS 中打开本仓库时，编译缓存位于外层 `out/build/standalone/<配置>`；从外层入口构建时使用 `out/build/x64-Debug` 或 `out/build/x64-Release`。程序统一输出到外层 `out/bin/<配置>`。外部 SDK 可通过 CMake 缓存路径配置，见构建说明；发布前仍须核查 [第三方资源授权](THIRD_PARTY_NOTICES.md)。

## 当前程序入口

2026-10-19 整理后，推荐启动外层 `out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`。正式图标位于 `resources/icons`，资源分类与维护约定见 `docs/development/REPOSITORY_LAYOUT.md`。

当前代码按 `Seis2D`、`Seis3D`、`SeisCommon` 组织，二维/三维支持 CPU/OpenMP 与 CUDA 自动回退、参数对照及性能对比。[代码架构与维护入口](docs/development/CODE_ARCHITECTURE.md)。
