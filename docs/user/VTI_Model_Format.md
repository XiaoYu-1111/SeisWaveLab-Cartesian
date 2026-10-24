# 二维与三维 VTI 物性模型

更新日期：2026-10-19。Cartesian 以 **VTI / VTK XML ImageData** 为主要模型格式；一个文件保存 Vp、Vs、密度与网格间距。二维保留 SEG-Y 物性三文件组作为外部交换接口；接收器时间序列继续使用 SEG-Y。S3DM 不再提供。

## 数据布局

| 内容 | 约定 |
| --- | --- |
| 二维网格 | `NX×1×NZ`，位于 X–Z 平面 |
| 三维网格 | `NX×NY×NZ` |
| 几何 | `Origin`、`Spacing` 与 `WholeExtent`，无需逐点存 XYZ |
| 物性 | `PointData` 标量 `Vp`、`Vs`、`Density` |
| 单位 | 长度 m，速度 m/s，密度 kg/m³，Z 向下为正 |
| 顺序 | X 最快，然后 Y、Z，`q=(z*ny+y)*nx+x` |
| 导出 | Float32，小端、未压缩 raw-appended，UInt64 长度块头 |
| 导入 | 未压缩 raw-appended 或 ASCII；Float32/Float64，小端/大端、UInt32/UInt64 块头 |
| 区域 | 仅物理节点，不含 CPML、ghost 或显示增益 |

三项 Float32 物性的主要存储量为 **12×节点数 字节**，另加约 2 KB XML 与元数据。例如 200³ 约 96 MB（91.6 MiB），二维 1000×1000 约 12 MB（11.4 MiB）。不额外保存坐标，不使用文本数值表示。

## 保存与载入

二维在 **模型文件 → VTI 模型（主要格式）** 中选择输入或输出路径，点击载入或保存；SEG-Y 交换组放在后面。三维同样在 **模型文件** 页选择独立的输入、输出路径后显式操作。默认路径位于程序旁 `data_Seis2D` / `data_Seis3D`，输入 `model.vti`，输出 `model_export.vti`。

采集期间不能操作模型文件。导出保存当前生效物性；已有文件不会覆盖，完整写入临时文件后再发布。载入先完整校验材料和几何，再重建零波场。模型文件不保存震源、接收器、记录、动态场或完整续算状态。

点击“浏览 VTI 模型”后进入系统文件选择器，选择已有 `.vti` 文件；“选择 VTI 模型输出”可选择保存目录并填写新文件名。若输入框中的目录尚不存在，选择器会回退到最近的有效父目录，支持中文目录和文件名。取消保持原输入；相对路径统一以程序目录为基准，浏览结束恢复原工作目录。

![三维模型文件页](../assets/seis3d/full/05-model-io.png)

## 三维导入进度

三维点击校验并导入后，通过后台任务校验并读取文件、初始化 CPU/CUDA 核心、上传物理材料；弹窗显示旋转动画、当前阶段、百分比与已用时间。窗口继续绘制，相关参数和运行操作暂时禁用。校验/读取阶段可取消，取消或格式校验失败保留原模型并保持暂停；开始初始化后等待完成。图形资源的最后安装仍在主界面线程执行。

![真实约 50 MB 模型导入进度](../assets/seis3d/import-progress.png)

二进制材料按块读取，避免每个浮点值分别查询文件位置。异步导入只做一次完整材料校验，再读取到任务私有物性缓冲，初始化完成后释放该缓冲；额外主机内存约为每物理节点 12 字节。完整校验后才释放旧计算缓冲，避免同时占用两套大型 GPU 会话。初始化或显示安装失败会报告错误，需重新初始化或导入。详情与本机验证条件见[异步导入验证](../reviews/VTI_ASYNC_IMPORT_2026_10_19.md)。

## 当前求解器约束

要求原点 `(0,0,0)`，零起始 Extent、正向轴对齐、一个 Piece。三维 XYZ 间距相等；二维 X/Z 间距相等、Ny=1。旋转、曲面、非等距网格需要先重采样。二维 X/Z 各为 64–8192，公共读入上限为 3600 万物理节点，三维还检查 4 GiB 会话预算、CFL 和设备限制。

物性必须有限，密度和 Vp 正，Vs 非负且小于 Vp，体积模量正。当前不读取压缩、base64、inline binary 或多 Piece 文件；外部 VTK 工具应先另存为未压缩 raw-appended 或 ASCII。

`FieldData` 可保存 `Dimension`、`CPMLThickness`、`BoundaryMode`、二维 `SolverScheme` 和 SI 单位标记。普通外部文件可省略这些元数据，按 SI 单位和正深度解释，缺省求解参数沿用当前设置。二维只接受对应二维的边界模式；二维与三维文件按网格维度分别载入。

曲网格项目使用 VTS 表示显式曲面坐标；两个项目采用同样的 VTK 字段名和单位，但文件不能作为完整会话直接互换。

## 外部查看与独立读取

使用 ParaView，或 Python 的 `vtkXMLImageDataReader` 打开模型并选择 Vp、Vs、Density。无需安装 VTK 即可运行仓库的[标准库读取示例](../examples/read_vti_model.py)：

```powershell
python docs/examples/read_vti_model.py ../out/bin/RelWithDebInfo/checks/vti-model/model-3d.vti
```

公共回归目标为 `SeisVtiModelRegression`；格式定义见 [VTK XML 官方规范](https://docs.vtk.org/en/v9.6.1/vtk_file_formats/vtkxml_file_format.html)。当前波场 VTI 快照包含分量和时刻，物性模型包含三个材料字段，见[数据交换说明](VTK_Model_Export.md)。
