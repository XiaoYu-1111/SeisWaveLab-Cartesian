# 2026-10-19 截图与文档更新

本轮从当前 `SeisWaveLab_Cartesian.exe` 运行内置截图流程，更新仓库内的流程图片与说明。构建配置为外层 `out/build/x64-Release` / RelWithDebInfo；增量构建检查结果为无需重新编译。

## README 与文档结构

README 对齐同系列 `SeisWaveLab_Curvilinear/README.md` 的顺序：名称与图标、简介和文档/性能入口、主页截图、维度说明、模拟与显示、仓库与输出目录、构建与运行、文档/博客/资源、操作与来源。保留 Cartesian 自身算法、边界、精度和构建依赖说明。

正式图标使用 `resources/icons/SeisWaveLab.png`；主页图片使用 `docs/assets/seis3d/full/01-home.png`。README 的图片、说明和依赖资源均在内部 Git 工作树内，可通过仓库相对路径阅读。

修订理论主文、二维指南、三维指南与导航中的旧描述，包括 CPU/OpenMP 后端、三模块源码布局、左侧控制/参数、SEG-Y 物性导入、外置吸收域、采样模式及最新性能报告入口。

## 本轮真实采集

| 项目 | 二维 | 三维 |
| --- | --- | --- |
| 数量 | 11 张 | 33 张 |
| 原始客户区 | 2560×1494 | 2560×1494 |
| 实际计算后端 | CUDA Float32 | CUDA Float32 |
| 物理网格 | 256×160，h=1 m | 200³，h=10 m |
| 求解时步 | 0.1 ms | 0.5 ms |
| Ricker 主频 | 60 Hz | 20 Hz |
| 接收与记录 | 100 道×500 样点，0.2 ms | 150 道×1000 样点，计算步距 2，1 ms |
| 典型波场图 | 第 560 步，0.056 s | 第 480 步，0.240 s |
| 新界面要素 | 左侧当前参数、B 外置边界、展开/折叠、记录分析器 | 左侧控制/显示/参数、体视图、全部道记录与采样设置 |

截图设备为 NVIDIA GeForce RTX 4060 Laptop GPU，计算能力 8.9。主题为 Paper Sand；示例参数显式设置，不等于应用普通默认值。三维图 09–14 使用同一第 480 步冻结波场切换色带。二维冻结记录来自第 1000 步时已有样本，未将未采集的 0.25 s 缓存尾段画入道集。

所有图片从真实 OpenGL 帧缓冲保存，复制到仓库时保持 PNG 字节不变，无裁切、缩放、重新着色或波形合成。`docs/assets/seis2d/capture-manifest.json` 和 `docs/assets/seis3d/full/capture-manifest.json` 保存日期、分辨率、SHA-256 和实际程序摘要。二维目录中的两张 CPU 回退验证截图独立保留，不计入这 44 张 CUDA 流程图。

外层原始输出为 `out/screenshots/application/2d` 与 `out/screenshots/seis3d-full`。运行日志位于 `out/screenshots/refresh-2026-10-19`，正式采集数据与模型保存在各截图流程的独立输出目录。复现命令见[文档导航](../README.md)与[文档工具](../tools/README.md)。

## 核对范围

核对完整窗口图片、流程清单、同一时刻色带比较、文件摘要、README 章节顺序和本地资源引用。文档发布工具检查原图尺寸、日期、清单数量与摘要，并生成离线包和 Hexo 包。检查摘要随本页附带的 [JSON 记录](DOCUMENTATION_REFRESH_2026_10_19.json)保存。

本轮只重新采集界面与整理说明；既有后端回归和性能数据仍保留各自的条件与日期。物理精度与数值边界见[后端验证](../development/BACKEND_VALIDATION.md)、[理论主文](../PROJECT_MANUAL.md)和[正式性能报告](../performance/CPU_CUDA_REPORT.md)。
