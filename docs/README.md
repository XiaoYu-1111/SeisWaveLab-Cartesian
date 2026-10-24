# SeisWave Lab 文档导航

文档索引整理：**2026-10-24**。项目使用规则笛卡尔网格与平面地表，二维与三维各自维护计算会话。界面截图、性能测试和历史审查保留原采集/实验日期；本轮整理文档结构，不重新开展数值实验。

推荐阅读顺序：**[项目总览](../README.md) → [理论说明](PROJECT_MANUAL.md) → [代码架构](development/CODE_ARCHITECTURE.md) → 对应操作指南 → 验证与文献**。仓库内 Markdown 为维护源，外层 `out/documentation`、`out/blog` 和 Word 文件是生成产物。

## 项目、原理与架构

| 阅读目标 | 入口 |
| --- | --- |
| 背景、方法、实现、界面、场景与参考文献 | [项目 README](../README.md) · [English](../README.en.md) |
| 速度—应力方程、交错网格、CFL、边界与采样 | [完整理论与流程](PROJECT_MANUAL.md)，保留 19 组编号公式 |
| 三模块分层、代码入口、调用链路与状态生命周期 | [代码架构](development/CODE_ARCHITECTURE.md) |
| 数值方法和数据标准的原始资料 | [参考文献及功能对应关系](REFERENCES.md) |

## 操作与数据交换

| 阅读目标 | 入口 |
| --- | --- |
| 二维建模、接收采样、记录查看与单炮包 | [二维用户指南](user/Seis2D_User_Guide.md) |
| 三维建模、源检布设、空间显示与导出 | [三维用户指南](user/Seis3D_User_Guide.md) |
| 33 张三维完整窗口及算例条件 | [截图集](user/Seis3D_Screenshot_Gallery.md) |
| Vp/Vs/密度的 VTI 模型、几何限制与独立读取 | [模型格式](user/VTI_Model_Format.md) |
| 物性模型和当前波场快照的区别 | [VTK 数据交换](user/VTK_Model_Export.md) |
| 三维 VTI 后台导入、进度与约 50 MB 本机验证 | [异步导入验证](reviews/VTI_ASYNC_IMPORT_2026_10_19.md) |
| Unicode 选择器、目录回退与取消行为 | [路径选择修复](reviews/FILE_DIALOG_FIX_2026_10_19.md) |
| 历史实操录像与录制时参数 | [视频说明](user/Seis3D_Video_Tutorial.md) |

## 构建、开发与验证

| 阅读目标 | 入口 |
| --- | --- |
| VS、SDK、构建缓存、程序输出与分发 | [构建说明](build/BUILDING.md) |
| Git 仓库、历史资源与外层生成文件 | [目录组织](development/REPOSITORY_LAYOUT.md) · [项目命名](development/PROJECT_NAMING.md) |
| 逐文件迁移关系 | [迁移清单](development/SOURCE_MIGRATION_2026-10-19.json) |
| CPU/OpenMP、CUDA、采样和记录回归的范围 | [后端验证](development/BACKEND_VALIDATION.md) |
| 二维外置吸收层与代码职责 | [外置边界](development/EXTERNAL_DOMAIN_2D.md) · [二维实现](development/SEIS2D_REFACTOR_AND_VIEWER.md) |
| 三维 CPML、自由表面、预算和数据头 | [三维实现](development/SEIS3D_IMPLEMENTATION.md) |
| 32 线程、二维/三维六档模型实测 | [性能报告](performance/CPU_CUDA_REPORT.md) · [汇总 CSV](performance/data/comparison.csv) · [复现步骤](../benchmarks/README.md) |
| 同系列界面主题 | [主题规范](development/THEME_SERIES.md) |

## 文档与研究资料

| 阅读目标 | 入口 |
| --- | --- |
| 截图清单、公式渲染、离线包与 Hexo 生成 | [文档工具](tools/README.md) · [Hexo 数学设置](build/HEXO_MATH.md) |
| 可在仓库直接阅读的博客文章 | [CUDA Seis3D](showcase/CUDA_Seis3D_GitHub_Pages.md)，从完整说明生成 |
| 原截图与资源核对记录 | [2026-10-19 文档更新](reviews/DOCUMENTATION_REFRESH_2026_10_19.md) |
| 定量实验与论文准备 | [研究路线](publication/PUBLICATION_ROADMAP.md) |

## 界面截图与采集条件（2026-10-19）

本轮流程图片于 2026-10-19 重新采集：三维 33 张、二维 12 张，原生 **2560×1494 完整客户区**，无裁切、缩放或重新着色。二维新增外置吸收边界、参数全部展开与折叠。三维为 200³、10 m、20 Hz、0.5 ms、50×3 接收点；六种色带冻结于第 480 步。正式采集 150 道×1000 样点、1 ms 间隔，首末速度时刻 0.75–999.75 ms。二维为独立 256×160、100 道×500 点算例。

普通默认值仍为三维 300×50×300、100×5 共 500 道、单炮、1000 点、自动 CFL 时步、Vz / Paper scientific / Coolwarm、增益 1.8、历史峰值保持关闭。不要把截图演示参数写成默认值。

当前图在 `assets/seis3d/full` 和 `assets/seis2d`，各目录有采集日期、程序摘要与 SHA-256 清单。二维目录另外保留两张 CPU 回退验证截图，其说明与本轮 CUDA 流程分开。旧局部图在 `assets/history/seis3d`，旧审查引用保留。历史验证记录保持其原日期，不以新截图代替数值精度证明。

## 重新采集与打包

先从外层工作区运行：

```powershell
& ./out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe --capture-seis3d-workflow
& ./out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe --capture-seis2d-workflow
```

再从内层 Git 仓库运行：

```powershell
python -B docs/tools/prepare_paper_screenshots.py
python -B docs/tools/prepare_seis2d_screenshots.py
python -B docs/tools/package_documentation.py
```

文档工具需要 Pillow。先复核真实图像和条件，再发布。维护源为 `PROJECT_MANUAL.md` 与分类用户指南，生成文件不作为反向编辑源；`package_showcase.py` 保留为兼容入口。

| 外层输出 | 用途 |
| --- | --- |
| `out/documentation/SeisWaveLab` | 完整离线 Markdown、45 张原图、配套格式与读取代码 |
| `out/documentation/SeisWaveLab_Documentation.zip` | 离线包 |
| `out/blog/SeisWaveLab/hexo/source/_posts/CUDA_Seis3D.md` | Hexo 文章 |
| 同级 `CUDA_Seis3D` 目录 | 同名资源文件夹；必须和文章一起复制 |
| `out/blog/SeisWaveLab_Hexo.zip` | Hexo 发布包 |
| `out/docs-work/SeisWaveLab` | HTML 数学排版预览与内部图像检查 |

站点启用 `post_asset_folder`，并把文章资源目录加入 `skip_render`，避免配套 Markdown 被识别为额外文章；数学配置参考 `build/HEXO_MATH.md`。本轮仅输出文档包，不修改博客站点或自动部署。既有 Word 导出工具仍可用，Word 文件留在外层 `out/docs`；本次不重新生成 Word。

## 历史与进一步审查

- [二维数值稳定性](reviews/NUMERICAL_STABILITY_REVIEW.md)
- [体绘制与六面吸收](reviews/SEIS3D_VOLUME_BOUNDARY_REVIEW.md)
- [显示状态与接收选行](reviews/SEIS3D_DISPLAY_STATE_RECEIVER_REVIEW.md)
- [分量尺度与单炮导入](reviews/SEIS3D_COMPONENT_SCALE_SHOT_IMPORT_REVIEW.md)
- [双语与折叠面板](reviews/SEIS3D_BILINGUAL_COMPACT_UI_REVIEW.md)
- [原始架构审查](reviews/DEVELOPMENT_ARCHITECTURE_REVIEW.md)
- [原二维 README](history/Seis2D_Original_README.md)
