# VTK 模型与波场数据交换

更新日期：2026-10-19。二维与三维模型以 **VTI ImageData** 为主，详细约定见 [VTI 模型格式](VTI_Model_Format.md)。旧 S3DM 模式已移除；二维 SEG-Y 物性交换接口保留。当前波场标量快照保留独立的 **VTI ImageData** 导出，不作为模型导入文件。

| 数据 | 格式 | 内容 |
| --- | --- | --- |
| 物性模型 | `.vti`，raw appended ImageData | 原点、间距与 Vp/Vs/Density；支持导入和导出 |
| 当前波场快照 | `.vti`，raw appended ImageData | 当前所选分量与时刻；仅导出 |

两个输出都使用完整物理网格，排除 CPML、ghost 和显示增益。图形色带、透明度和视图缩放不改变保存数值；两类文件都不是动态场续算检查点。

## 模型操作

三维 **Model I/O / 模型文件** 页中，选择 `.vti` 后点击校验导入；导出时选择独立的新 `.vti` 文件名，点击“导出 VTI 模型到新文件”。保存当前已初始化物性，待应用草稿不参与导出。文件使用Origin / Spacing 隐式几何及 Vp、Vs、Density，单位为 m、m/s、kg/m³，Z 为正深度。

文件可由标准 VTK/ParaView 读取。Cartesian 导入支持未压缩 raw-appended 或 ASCII Float32/64，要求零原点、正向、轴对齐、三轴相同间距；曲面网格需先重采样。更多几何和字段要求见[VTI 格式说明](VTI_Model_Format.md)。

## 当前波场快照

1. 运行至希望保存的时刻，暂停后选择 `Vz`、`Vx`、`Vy`、速度模或压力。
2. 在 Model I/O 中选择独立的 **Wavefield snapshot path**，使用新的 `.vti` 文件名。
3. 点击 **Export current wavefield snapshot (.vti)**，等待完成。大模型读回与写盘期间界面可能短暂停顿。

| 分量 | PointData 名称 | 单位 |
| --- | --- | --- |
| 速度 | `Vz`、`Vx`、`Vy` | m/s |
| 速度模 | `VelocityMagnitude` | m/s |
| 压力 | `Pressure` | Pa |

快照使用小端 Float32、无压缩 raw appended、UInt64 长度块头；原点零，间距为当前 h，X 最快。FieldData 保存模拟步数、名义时间、dt、分量、单位代码与正深度标记。导出不应用色带、增益、阈值或体显示降采样。

速度由相邻半格点算术平均重构到整数参考格点，速度模从三个重构速度计算，压力由整数格点法向应力计算。空间重构不改变其实际时间层：N≥1 时速度/速度模位于 `(N−0.5)dt`，压力位于 `Ndt`；`SimulationTimeSeconds` 记录工具栏名义时刻 `Ndt`。跨程序比较须保留半时间步差异。

## 外部查看

在 ParaView 打开模型 `.vti`，选择 Vp、Vs 或 Density；打开波场 `.vti` 时选择文件内的波场分量。点击 Apply，按需切片或体绘制，并调整相机与色标。两种文件均使用 Z 正深度，外部查看器可能需要调整相机方向。

Python VTK 中，模型使用 `vtkXMLImageDataReader`，波场使用 `vtkXMLImageDataReader`：

```python
from vtkmodules.vtkIOXML import vtkXMLImageDataReader

reader = vtkXMLImageDataReader()
reader.SetFileName("model_export.vti")
reader.Update()
model = reader.GetOutput()
print(model.GetDimensions())
for name in ("Vp", "Vs", "Density"):
    print(name, model.GetPointData().GetArray(name).GetRange())
```

Python 示例需要用户环境中的 VTK 包，软件本体无需 VTK SDK。小型模型也可使用仓库[标准库读取示例](../examples/read_vti_model.py)。格式依据见 [VTK XML 规范](https://docs.vtk.org/en/v9.6.1/vtk_file_formats/vtkxml_file_format.html)。
