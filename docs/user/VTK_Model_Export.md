# 三维模型与波场快照导出为 VTK ImageData（.vti）

适用：SeisWave Lab / Seis3D。更新日期：2026 年 10 月 18 日。

当前模型文件、当前波场快照出口的完整界面见下图。物性 VTI 和波场 VTI 为两个独立操作，以下格式表的 Vp/Vs/Density 针对物性导出。

![模型格式、路径与当前波场 VTI 导出](../assets/seis3d/full/05-model-io.png)

## 1. 采用的格式

本功能导出 **VTK XML ImageData，扩展名 `.vti`**，用于交换三维规则网格物性模型或一个时刻的波场分量快照。当前等距 XYZ 网格可用尺寸、原点与间距描述，适合该格式。[VTK 官方规范](https://docs.vtk.org/en/latest/vtk_file_formats/vtkxml_file_format.html)

| 项目 | 当前实现 |
| --- | --- |
| VTK 数据类型 | ImageData |
| 数据关联 | PointData，网格点物性 |
| 数组 | `Vp`、`Vs`、`Density` 三个独立标量 |
| 数值类型 | IEEE 754 Float32，小端 |
| 编码 | AppendedData raw 二进制，不压缩 |
| 二进制块头 | UInt64，小端，每个数组前保存其载荷字节数 |
| 原点 | `(0,0,0)`，局部模型坐标 |
| 间距 | 当前模型 `dx=dy=dz=spacing`，单位 m |
| 数组顺序 | X 最快，然后 Y，最后 Z |
| 坐标方向 | Z 为向下的正深度 |
| 导出区域 | 完整物理网格，不包含 CPML 扩展区或 ghost 单元 |

S3DM 导入/导出继续可用；VTI 是新增的导出选项。当前模型导入入口仍读取 `.s3dm`，未增加 VTI 导入。

## 2. 在软件中导出

1. 进入三维模拟工作区，在 **Simulation / 模拟** 中设置模型并点击 **Initialize 3D / 初始化三维**。修改待应用参数后，需要先 **Rebuild 3D session / 重建三维会话**。
2. 打开 **Model I/O / 模型文件**。
3. 将 **Model export format / 模型导出格式** 切换为 **VTK ImageData (.vti)**。
4. 点击 **Choose model output / 选择模型输出位置**，选择文件夹与新的 `.vti` 文件名；也可直接编辑 **Model export path / 模型导出路径**。
5. 点击 **Export VTI model to a new file / 导出 VTI 模型为新文件**，等待底部导出完成提示。

默认 VTI 路径为程序旁 `data_Seis3D/model_export.vti`。S3DM、VTI 各自保存独立输出路径，切换格式不会将此前输入的路径改写成另一格式。浏览或切换格式本身不导出文件。

已有目标文件不覆盖，请选择新文件名。手动输入的 VTI 输出文件名须以 `.vti` 结尾。采集期间模型文件操作被锁定；普通运行或暂停时可以导出，导出不重置波场、时间、接收系统、显示增益或采集结果。

### 导出当前波场快照

1. 初始化模型并运行至希望保存的时刻，在 **Display / materials** 中选中波场分量。
2. 在 **Model I/O** 中编辑 **Wavefield snapshot path**，或点 **Choose snapshot output** 选择一个新的 `.vti` 文件名。
3. 点击 **Export current wavefield snapshot (.vti)**。该操作会逐深度平面从 GPU 下载数据，较大网格可能使界面短暂停顿；不会重置或修改模拟状态。
4. 使用 ParaView 打开文件并点击 **Apply**，在 Point Data 中选择同名分量数组。

导出的单数组和单位如下：

| 当前分量 | `.vti` PointData 名称 | 单位 |
|---|---|---|
| Vz、Vx、Vy | Vz、Vx、Vy | m/s |
| 速度大小 | `VelocityMagnitude` | m/s |
| Pressure | `Pressure` | Pa |

波场文件的 FieldData 保存模拟步数、模拟时间、时间步长、分量编号、单位代码和正深度标记。保存的是求解器物理值，不应用显示增益、色带、阈值或体绘制重采样。每个文件只对应一个时刻；需要多个时刻时，请为每次导出选不同文件名。它不是时间序列或可恢复的模拟检查点。

空间上，速度分量由对应轴上相邻两个半格点取算术平均，重构到整数参考格点；速度大小由三个重构分量计算，压力由三个法向应力计算。输出仍是完整物理网格分辨率。

时间上，当前 `SimulationTimeSeconds` 保存 `SimulationStep × dt` 的工具栏名义时间。完成 N≥1 步时，速度与速度模实际位于 `(N−0.5) × dt`，压力位于 `N × dt`；代码未做时间同位插值。跨程序比较时应保留这半时间步差异。初始全零状态另按初始场处理。

**导出的是当前已初始化模型。** 左侧尚未重建的尺寸、材料、边界等草稿不参与导出。这个约定避免文件标注与实际 GPU 模型不一致。

## 3. 导出的数据含义

| 数组名称 | 单位 | 含义 |
| --- | --- | --- |
| `Vp` | m/s | 纵波速度 |
| `Vs` | m/s | 横波速度 |
| `Density` | kg/m³ | 密度 |

这些值来自 GPU 模型的物理格点，导出时由内部材料参数换算回来，可能包含 float32 舍入。波场增益、模型色带、切片缩放、显示降采样和边界层显示开关不参与材料写出。

物性模型文件不含当前波场、地震记录、震源、接收排列或续算状态；导出物性模型不要求先运行或发炮。波场快照是另一类单标量 VTI，包含其时刻与分量元数据。

### 3.1 网格坐标和范围

模型具有 `nx×ny×nz` 个采样点，对应：

```text
WholeExtent = 0 nx-1 0 ny-1 0 nz-1
Origin      = 0 0 0
Spacing     = spacing spacing spacing
pointIndex  = (z * ny + y) * nx + x
position    = (x * spacing, y * spacing, z * spacing)
```

默认 `300×50×300`、10 m 间距时，坐标范围为 X=0–2990 m、Y=0–490 m、Z=0–2990 m。这里保存的是网格点数据；VTK 的几何单元数量与采样点数量不同，不能将两者混用。

**Z 数值随深度增加。** 外部软件可能默认把正 Z 方向显示为向上，需要调整相机或显示变换来匹配地球物理习惯；文件不会自动翻转数组或把深度改成负数。文件不包含地理投影、世界坐标原点偏移或旋转测网。

### 3.2 FieldData 元数据

除三个 PointData 物性数组，还写入以下数据集级标量：

| 名称 | 值与含义 |
| --- | --- |
| `BoundaryMode` | 0=六面 CPML；1=自由顶面加其余五面 CPML；2=六面滑移反射壁 |
| `CPMLThickness` | 用户设置的 CPML 格数；反射模式保留参数，但求解时禁用 CPML |
| `DepthPositiveDown` | 1，表示 Z 是正深度 |
| `LengthUnitInMeters` | 1，坐标和间距使用米 |
| `VelocityUnitInMetersPerSecond` | 1，Vp/Vs 使用 m/s |
| `DensityUnitInKilogramsPerCubicMeter` | 1，密度使用 kg/m³ |

这些名称是本项目在标准 VTK FieldData 中定义的元数据约定。外部查看器可读取它们，但不会自动据此配置边界求解器、翻转坐标或调整单位。

## 4. 在 ParaView 中打开

1. 选择 **File → Open**，打开导出的 `.vti` 文件。
2. 点击 **Apply**。
3. 在标量选择中选择 `Vp`、`Vs` 或 `Density`。
4. 按需要使用剖切或体显示，调整相机、透明度与色标范围。

ParaView 会根据文件类型选择读取器；属性或数组选择修改后需应用才能更新。[ParaView 官方读取说明](https://docs.paraview.org/en/latest/UsersGuide/dataIngestion.html)

VTI 保存数值和网格，不携带 SeisWave Lab 的 Paper scientific、Coolwarm 显示参数。外部软件里的色带、材料透明度和相机应单独配置。

## 5. 在 Python VTK 中读取

以下代码需自行安装 Python VTK 包；软件本体的 VTI 导出不依赖该包或完整 VTK SDK。

```python
from vtkmodules.vtkIOXML import vtkXMLImageDataReader

reader3D = vtkXMLImageDataReader()
reader3D.SetFileName("D:/Models/model_export.vti")
reader3D.Update()
image3D = reader3D.GetOutput()
print("Dimensions:", image3D.GetDimensions())
print("Origin:", image3D.GetOrigin())
print("Spacing:", image3D.GetSpacing())
pointData3D = image3D.GetPointData()
for name3D in ("Vp", "Vs", "Density"):
    values3D = pointData3D.GetArray(name3D)
    if values3D is None:
        raise ValueError(f"Missing material array: {name3D}")
    print(name3D, values3D.GetNumberOfTuples(), values3D.GetRange())
```

如转换为 NumPy，保持 `[nz,ny,nx]` 顺序：

```python
from vtkmodules.util.numpy_support import vtk_to_numpy

nx3D, ny3D, nz3D = image3D.GetDimensions()
vp3D = vtk_to_numpy(pointData3D.GetArray("Vp")).reshape(nz3D, ny3D, nx3D)
```

此处为外部读取用法示例；本轮实现完成发布版编译检查，未执行 ParaView 或 Python VTK 的端到端读取测试。

## 6. 二进制布局与导出开销

导出采用 VTK 支持的附加原始二进制。每个数组连续存放，结构为：

```text
XML header and grid metadata
<AppendedData encoding="raw">
_
[UInt64 Vp_bytes][all Vp float32]
[UInt64 Vs_bytes][all Vs float32]
[UInt64 Density_bytes][all Density float32]
</AppendedData>
</VTKFile>
```

上图仅示意；三个块之间没有额外换行或分隔符。各数组的 `offset` 从下划线之后计数，指向其8字节长度头。令 `N=nx×ny×nz`、`B=8+4N`，三个偏移依次为 `0`、`B`、`2B`。

`raw` 附加数据是 VTK 支持的编码方式，二进制部分可能不符合普通 XML 文本解析器的要求。请使用 VTK ImageData 读取器，不要用通用 XML 编辑器完整解析材料载荷。[VTK 对 raw 编码的说明](https://docs.vtk.org/en/latest/vtk_file_formats/vtkxml_file_format.html#xml-file-formats)

当前导出不压缩、不按显示网格抽稀。默认模型的材料载荷约54 MB，最大3600万物理节点约432 MB，另加少量 XML 与长度头。实现按 XY 层下载并分离属性，CPU 临时缓冲随平面大小增长，不要求保存完整模型副本；导出仍在界面线程执行，大模型写盘时界面可能短暂停顿。

文件先写入目标旁独立临时目录，完成后再改为目标文件名；正常完成或捕获错误时清理临时文件。失败提示后可更换位置或释放磁盘空间再重试。VTI 未增加内容校验和；科研共享时建议另存 SHA-256。

## 7. 对应实现与其他资料

- [`VtiExport3D.cpp`](../../src/Seis3D/VtiExport3D.cpp)：材料下载、XML/二进制写入与失败清理。
- [`Workspace3D.cpp`](../../src/Seis3D/Workspace3D.cpp)：模型导出格式、路径与按钮。
- [`FileDialogs3D.cpp`](../../src/Seis3D/FileDialogs3D.cpp)：Windows Unicode 保存位置选择。
- [三维模拟使用指南](Seis3D_User_Guide.md)。
- [S3DM 格式与独立读取代码](S3DM_Model_Format.md)。
