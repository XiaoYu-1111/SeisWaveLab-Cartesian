# 参考文献与方法对应关系

整理与元数据核对日期：2026-10-24。此页为 [README](../README.md) 和[完整理论说明](PROJECT_MANUAL.md)提供文献入口；引文用于解释方法背景，项目具体离散、边界、源归一化和格式支持范围以源码与验证记录为准。

## 数值方法

1. **Virieux, J. (1986).** P-SV wave propagation in heterogeneous media; velocity-stress finite-difference method. *Geophysics, 51*(4), 889–901. [DOI: 10.1190/1.1442147](https://doi.org/10.1190/1.1442147)。

   对应速度—应力一阶系统、完全交错网格和二维 P–SV 建模背景。作者、年份、卷期与页码核对自 [SEG 出版页](https://pubs.geoscienceworld.org/seg/geophysics/article-abstract/51/4/889/68738/P-SV-wave-propagation-in-heterogeneous-media?redirectedFrom=fulltext)。

2. **Levander, A. R. (1988).** Fourth-order finite-difference P-SV seismograms. *Geophysics, 53*(11), 1425–1436. [DOI: 10.1190/1.1442422](https://doi.org/10.1190/1.1442422)。

   对应高阶交错差分、数值频散、自由表面与基准算例的研究背景。该论文讨论四阶空间、二阶时间方案；本项目当前光滑内部采用八阶差分，其权重与实现见 [SolverNumerics.h](../include/Seis2D/SolverNumerics.h)、[SolverCells3D.h](../src/Seis3D/SolverCells3D.h)。书目信息参见 [Crossref 的出版方记录](https://chooser.crossref.org/?doi=10.1190%2F1.1442422)与[论文副本](https://csim.kaust.edu.sa/files/ErSE210/Levander.PSV.pdf)。

3. **Komatitsch, D., & Martin, R. (2007).** An unsplit convolutional perfectly matched layer improved at grazing incidence for the seismic wave equation. *Geophysics, 72*(5), SM155–SM167. [DOI: 10.1190/1.2757586](https://doi.org/10.1190/1.2757586)。

   对应三维非分裂 CPML 的导数记忆变量与吸收边界背景。作者维护的 [SEISMIC_CPML 方法说明](https://specfem.org/komatitsch.free.fr/README_seismic_cpml.html)给出论文与 BibTeX。当前三维工程采用薄层存储；二维保留自己的 PML/自由表面方案，详见[三维实现](development/SEIS3D_IMPLEMENTATION.md)和[二维外置边界](development/EXTERNAL_DOMAIN_2D.md)。

## 数据格式

4. **SEG Technical Standards Committee. (2002).** SEG Y rev 1 Data Exchange format. Release 1.0, May 2002. [SEG 官方标准索引](https://seg.org/publications/seg-technical-standards/) · [官方 PDF](https://seg.org/wp-content/uploads/2025/11/seg_y_rev1.pdf)。

   对应道集文件的文本头、二进制头、道头、坐标和采样信息。程序的固定长度记录、数值编码与导入限制见[三维用户指南](user/Seis3D_User_Guide.md)和[二维用户指南](user/Seis2D_User_Guide.md)。引用标准不表示程序实现所有修订版、扩展头或所有数值编码。

5. **VTK Project.** XML File Formats — ImageData. [VTK 9.6.1 官方格式文档](https://docs.vtk.org/en/v9.6.1/vtk_file_formats/vtkxml_file_format.html)，访问日期：2026-10-24。

   对应 `.vti` 的 `WholeExtent`、`Origin`、`Spacing`、`PointData` 与 appended 数据。当前模型保存 Vp、Vs、Density；二维形状为 `NX×1×NZ`。程序使用自己的读写组件，支持范围、单位、节点顺序与编码见 [VTI 模型格式](user/VTI_Model_Format.md)和[独立 Python 示例](examples/read_vti_model.py)。

## 实现、验证与资料导航

| 阅读问题 | 项目资料 |
| --- | --- |
| 当前方程、差分权重、CFL 与时间层如何定义？ | [完整理论与流程](PROJECT_MANUAL.md) |
| 方法如何映射到共享组件和二维/三维核心？ | [代码架构](development/CODE_ARCHITECTURE.md) |
| 现有证据覆盖哪些算例与限制？ | [后端验证](development/BACKEND_VALIDATION.md) |
| CPU/CUDA 的实测条件与原始结果是什么？ | [性能报告](performance/CPU_CUDA_REPORT.md) |
| 图形与交互依赖从哪里来？ | [第三方来源与许可](../THIRD_PARTY_NOTICES.md) |

论文提出的方法、工程实现、可视化结果与定量精度验证是不同层次的资料。阅读时应结合具体网格、边界、物性与采样设置；本轮文献整理未重新开展数值实验。
