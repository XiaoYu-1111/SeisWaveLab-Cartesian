# SeisWave Lab 文档导航

更新日期：2026-10-18。当前项目采用笛卡尔规则网格、平面地表，二维和三维工作区独立；曲网格项目分别维护。本目录在内层 Git 仓库内，可和源码一起提交。

## 阅读入口

| 内容 | 文档 |
| --- | --- |
| 背景、理论、实现、完整操作和复现附录 | [完整说明](PROJECT_MANUAL.md)，包含 19 组编号公式与当前运行截图 |
| 三维建模、震源、显示、采集和导出 | [Seis3D 用户指南](user/Seis3D_User_Guide.md) |
| 二维建模、地震记录、AGC 和单炮包 | [二维用户指南](user/Seis2D_User_Guide.md) |
| 33 张三维完整窗口原图 | [截图集](user/Seis3D_Screenshot_Gallery.md) |
| S3DM 格式与 Python/C++ 读取器 | [自定义模型格式](user/S3DM_Model_Format.md) |
| VTI 物性模型与当前波场快照 | [VTK 数据交换](user/VTK_Model_Export.md) |
| VS、SDK、缓存与外层输出位置 | [构建说明](build/BUILDING.md) |
| Hexo 同名资源、NexT 与 MathJax SVG | [Hexo 公式设置](build/HEXO_MATH.md) |
| Git 仓库、文档分类与生成产物 | [文件组织](development/REPOSITORY_LAYOUT.md) |
| Cartesian 项目名、目录迁移与 VS 重新打开 | [项目命名](development/PROJECT_NAMING.md) |
| CUDA、CPML、自由表面、预算与 SEG-Y | [三维实现](development/SEIS3D_IMPLEMENTATION.md) |
| 二维文件职责、生命周期与记录查看器 | [二维整理](development/SEIS2D_REFACTOR_AND_VIEWER.md) |
| 本轮重新截图及发布检查 | [2026-10-18 更新记录](reviews/DOCUMENTATION_REFRESH_2026_10_18.md) |
| 科研验证、可复现性与投稿准备 | [论文路线](publication/PUBLICATION_ROADMAP.md) |
| 模型到采集视频 | [历史实操录像说明](user/Seis3D_Video_Tutorial.md)，保留录制时参数 |
| 仓库可读博客文章 | [CUDA Seis3D](showcase/CUDA_Seis3D_GitHub_Pages.md)，由完整说明同步 |

## 本次截图

全部图片于 2026-10-18 重新采集：三维 33 张、二维 8 张，原生 **2560×1494 完整客户区**，无裁切、缩放或重新着色。三维为 200³、10 m、20 Hz、0.5 ms、50×3 接收点；六种色带冻结于第 480 步。正式采集 150 道×1000 样点、1 ms 间隔，首末速度时刻 0.75–999.75 ms。二维为独立 256×160、100 道×500 点算例。

普通默认值仍为三维 300×50×300、100×5 共 500 道、单炮、1000 点、自动 CFL 时步、Vz / Paper scientific / Coolwarm、增益 1.8、历史峰值保持关闭。不要把截图演示参数写成默认值。

当前图在 `assets/seis3d/full` 和 `assets/seis2d`，各目录有采集日期与 SHA-256 清单。旧局部图移入 `assets/history/seis3d`，旧审查引用同步保留，不进入当前发布包。历史验证记录保持其原日期，不以新截图代替数值精度证明。

## 重新采集与打包

先从外层工作区运行：

```powershell
& ./out/bin/RelWithDebInfo/SeisWaveLab.exe --capture-seis3d-workflow
& ./out/bin/RelWithDebInfo/SeisWaveLab.exe --capture-seis2d-workflow
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
| `out/documentation/SeisWaveLab` | 完整离线 Markdown、41 张原图、配套格式与读取代码 |
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
