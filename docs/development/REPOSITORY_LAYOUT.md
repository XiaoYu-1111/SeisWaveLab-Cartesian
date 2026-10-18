# 文件分类与仓库维护

更新日期：2026-10-18。当前项目专注笛卡尔网格二维／三维模拟。

## 1. 两层目录

Git 仓库是内层 `CMakePro_CUDA_SeisSim_V2`；父目录包含本机外层 CMake 包装、VS 配置和统一 `out`。文档和发表截图必须在内层仓库，保证 VS 提交和克隆可获得这些资料。

| 目录 | 职责 | 保留方式 |
| --- | --- | --- |
| `Main_cpp`、`Main_header` | 应用入口、主页、双语、共享显示 | 源码 |
| `src/Seis2D`、`src/Seis3D` | 各维度会话、UI、采集与 I/O | 源码 |
| `include`、`cuda`、`CUDA_Kernels` | 物理结构与 CUDA 算法 | 源码 |
| `shaders`、`resources`、`resource_CUDA_V1` | 着色器及原始运行资源 | 源资源 |
| `ImGui_docking`、`ImPlot` 等 | 现有第三方依赖 | 保留许可，不随意移动 |
| `Pro_Model`、`Pro_Picture` | 旧版模型／图片资源 | 先核对引用，不能按名称认定冗余 |
| `docs/PROJECT_MANUAL.md` | 理论与流程的统一维护源 | 主要文档 |
| `docs/user` | 各维度操作、格式、图集 | 分类文档 |
| `docs/build`、`development` | 编译、Hexo、实现与本页 | 分类文档 |
| `docs/reviews`、`publication` | 历史审查与论文准备 | 保留其时间和证据范围 |
| `docs/history` | 旧说明 | 历史资料 |
| `docs/assets/seis3d/full`、`docs/assets/seis2d` | 本次完整 PNG 与摘要 | 当前发表图片 |
| `docs/assets/history/seis3d` | 旧局部截图和旧样式 | 历史图片；旧文档链接同步更新 |
| `docs/tools`、`docs/examples` | 发布工具与独立读数据示例 | 源工具 |

此次整理不重构数值源文件，不移动 SDK／第三方资源，不更改既有用户代码；历史审查保留原日期。

## 2. 统一外层 out

编译缓存为 `out/build/x64-Debug` 与 `out/build/x64-Release`；独立内层构建另用 `out/build/standalone/<配置>`。Debug 程序为 `out/bin/Debug`，当前 VS Release 为 `out/bin/RelWithDebInfo`。遵守根目录 AGENTS 约定。

| 外层 out 子目录 | 用途 |
| --- | --- |
| `build`、`bin`、`lib` | CMake 缓存、程序、运行资源及库 |
| `screenshots/seis3d-full` | 独立三维截图和本轮实际采集数据 |
| `documentation/SeisWaveLab` | 可离线阅读的图文包 |
| `blog/SeisWaveLab` | Hexo source 树、发布说明与 ZIP |
| `docs`、`docs-work` | 既有 Word 文件与排版中间数据 |
| `archive`、`refactor-backups` | 旧产物及人工备份 |
| `checks`、`tools`、`videos` | 检查输出、本机脚本及视频 |

用户采集数据、录像、备份及旧项目暂存不作自动删除；先确认用途再归档。`.vs` 是 IDE 缓存，不应提交；VS 使用期间也不为目录整洁而强删。

## 3. 更新说明

主文 `PROJECT_MANUAL.md` 是理论文章的维护源；`docs/showcase/CUDA_Seis3D_GitHub_Pages.md` 由打包工具同步 front matter 与相对链接，供仓库内阅读。完整步骤继续维护 `docs/user`，打包生成的 `out` 副本不反向作为源。

```powershell
python -B docs/tools/prepare_paper_screenshots.py
python -B docs/tools/prepare_seis2d_screenshots.py
python -B docs/tools/package_documentation.py
```

打包仅包含白名单文档、当前 41 张完整图片、示例及来源说明，不把 EXE、PDB、模型体、SEG-Y、旧裁切图或渲染缓存加入博客 ZIP。检查结果记录于发布包 `package-validation.json`。本机输出不加入 Git，不自动提交或推送。
