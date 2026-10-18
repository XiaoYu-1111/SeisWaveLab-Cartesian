# S3DM 模型格式与独立读取说明

适用：SeisWave Lab / Seis3D，S3DM 文件版本 1、2。更新日期：2026 年 10 月 15 日。

## 1. 是否为通用格式

**本项目的 `.s3dm` 是 SeisWave Lab 自定义的三维弹性介质模型容器，不是 SEG-Y，也不是通用三维网格交换标准。** 其他软件不会因为识别扩展名就自动理解文件内容，需要按下述规则读取或编写转换器。扩展名不能代替魔数与版本检查。

文件保存规则与下列当前实现对应：

- [`ModelIo3D.cpp`](../../src/Seis3D/ModelIo3D.cpp)：文件头、版本选择、大小检查、材料扫描与逐层导入/导出。
- [`Cuda_Seis3D.cu`](../../cuda/Cuda_Seis3D.cu)：`uploadModelPlane3D`、`downloadModelPlane3D`，确定材料排列与 Lamé 参数转换。
- [`Common3D.h`](../../include/Common3D.h)：当前程序的网格与内存约束。

配套读取代码：

| 代码 | 用途 | 依赖 |
| --- | --- | --- |
| [read_s3dm.py](../examples/read_s3dm.py) | 文件头、单格点随机读取、分块扫描全部材料、输出 JSON | Python 3.10+ 标准库 |
| [S3dmReader3D.h](../examples/S3dmReader3D.h) | 可直接包含的 C++ 读取器，支持单格点和逐 XY 层读取 | C++17 标准库 |

两个示例均不依赖 CUDA、OpenGL、ImGui，也不要求 NVIDIA 显卡。示例读文件，不改写文件。已通过 Python 语法编译与 MSVC C++17 编译，编译产物位于外层 `out/docs-work/s3dm-reader`；本轮未执行实际模型文件的读取回归测试。

## 2. 文件保存哪些内容

保存：

- 物理网格的 X/Y/Z 节点数。
- 三轴共用的等距网格间距，单位 m。
- CPML 厚度参数、边界模式。
- 各物理格点的 `Vp`、`Vs`、密度 `rho`。

不保存：

- 当前波场的速度、应力、CPML 记忆变量与模拟时间。
- 震源坐标、机制、振幅、主频。
- 接收排列、炮数、采样间隔与地震记录。
- CFL 系数、手动时步设置、显示增益、配色和视角。
- 地理坐标参考系、世界坐标原点、模型预设名称或参数。

因此，**模型文件不是完整工程文件，也不是可以续算的检查点**。载入后仍需在程序内配置震源、采集与时步等参数；程序重新扫描最大 Vp 并校验安全步长。模型保存的是采样后的物性，不能从文件恢复“断层”“盐丘”等预设的编辑参数。

## 3. 二进制文件头

文件头固定 **36 字节**，无填充字节。下表偏移从 **0** 开始，全部整数为无符号 32 位小端整数，间距为 IEEE 754 binary32。

| 偏移 | 长度/字节 | 类型 | 字段 | 含义 |
| --- | --- | --- | --- | --- |
| 0–7 | 8 | 原始字节 | magic | `53 33 44 4D 4F 44 31 00`，即 `S3DMOD1` 加一个 `\0` |
| 8–11 | 4 | uint32 LE | version | 1 或 2 |
| 12–15 | 4 | uint32 LE | nx | X 方向物理节点数 |
| 16–19 | 4 | uint32 LE | ny | Y 方向物理节点数 |
| 20–23 | 4 | uint32 LE | nz | Z 方向物理节点数 |
| 24–27 | 4 | uint32 LE | pml | CPML 厚度参数，单位：格 |
| 28–31 | 4 | uint32 LE | boundary | 边界模式代码，见下一节 |
| 32–35 | 4 | float32 LE | spacing | `dx=dy=dz`，单位 m |
| 36 起 | `12×nx×ny×nz` | float32 LE | materials | 格点物性交错三元组 |

Python 对应的头部格式串为 `struct.Struct("<8s6If")`，字节数为 36。`<` 明确表示小端与标准字段长度；不能换成隐含本机对齐的结构体布局。

### 3.1 字节序的实际边界

当前程序对文件头中的整数及间距位模式明确写入小端；材料载荷通过原生 `float` 缓冲直接写出。**在当前 Windows x64 运行平台，载荷为小端 IEEE float32。** 因此本说明和读取器明确按小端解析完整文件。

以后若将导出程序移植到大端平台，应显式转换载荷字节序，再沿用本格式；不能直接复用原生 `float` 写出路径。读取器不尝试根据错误魔数“猜测”端序。

## 4. 版本与边界模式

| boundary | 物理含义 | version=1 | version=2 |
| --- | --- | --- | --- |
| 0 | 六面 CPML 吸收 | 支持 | 支持 |
| 1 | 顶面 z=0 牵引力为零，其余五面 CPML | 支持 | 支持 |
| 2 | 六面反射滑移壁：法向速度为零、切向牵引力为零；禁用 CPML | 不支持 | 支持 |

当前导出器在边界代码为 2 时写入 **version=2**，其余边界写入 **version=1**。当前导入器同时支持两个版本；版本 2 也接受边界代码 0、1。两版的魔数、头部长度、载荷排列均相同，魔数中的字符 `1` 不代表文件头里的版本字段必须为 1。

反射模式的 `pml` 仍保留用户设置值，但不表示载荷中存在 CPML 层，也不表示求解时启用了吸收层。这里的反射模式是代码实现的滑移反射壁，不能等同于六个牵引力为零的自由表面。

## 5. 材料排列、单位与坐标

### 5.1 每个格点的三元组

```text
Vp(float32), Vs(float32), rho(float32)
```

- Vp：纵波速度，m/s。
- Vs：横波速度，m/s。
- rho：密度，kg/m³。

**三个属性交错保存**，不是先保存完整 Vp 体、再保存 Vs 体和 rho 体。

```text
z=0,y=0,x=0: Vp Vs rho
z=0,y=0,x=1: Vp Vs rho
...
z=0,y=1,x=0: Vp Vs rho
...
z=1,y=0,x=0: Vp Vs rho
```

X 变化最快，其次 Y，最后 Z。按 C 连续数组理解，形状为：

```text
materials[nz][ny][nx][3]
```

最后一轴 `0=Vp`、`1=Vs`、`2=rho`。注意：头部按 `nx,ny,nz` 给尺寸，数组按 `[z,y,x]` 寻址。

### 5.2 随机访问公式

对从 0 开始的物理索引 `(x,y,z)`：

```text
cellIndex   = (z * ny + y) * nx + x
cellOffset  = 36 + 12 * cellIndex
Vp offset   = cellOffset
Vs offset   = cellOffset + 4
rho offset  = cellOffset + 8
fileBytes   = 36 + 12 * nx * ny * nz
```

例如，默认 `300×50×300` 模型含 4,500,000 个物理格点，文件应为 **54,000,036 字节**，约 51.50 MiB。文件不压缩，没有尾部块、校验和或附加 JSON。

当前界面把物理坐标显示为 `(x×spacing, y×spacing, z×spacing)`；Z 为向下的深度方向，顶面为 Z=0。文件不保存投影、地理坐标原点或旋转参数。相邻首末格点的坐标跨度为 `(n-1)×spacing`，不能把节点数与间隔数混为一谈。

### 5.3 CPML 与 ghost 区域

载荷仅含 `nx×ny×nz` 物理格点，不包含 CPML 扩展层或 ghost 单元。程序载入后按边界设置重新分配扩展区域，并由物理边缘外推材料。文件尺寸计算时不能再给三轴增加 PML 厚度。

GPU 内部使用逆密度和 Lamé 参数；文件对外保存的是 Vp/Vs/rho：

```text
mu     = rho * Vs²
lambda = rho * (Vp² - 2*Vs²)
```

导出时从 GPU 材料参数反算回 Vp/Vs/rho，float32 舍入可能产生细微差异，不应承诺与此前输入文件逐字节一致。

## 6. Python 读取方法

在 Git 仓库根目录打开终端，文件路径按实际保存位置修改。

### 6.1 只读文件头

```powershell
python docs/examples/read_s3dm.py "D:/Models/model.s3dm"
```

输出 JSON 中包含尺寸、版本、边界名称、间距和文件大小。此模式只检查结构和长度，`payloadValidated3D` 为 false；不能据此判断所有材料都有效。

### 6.2 读取一个格点

```powershell
python docs/examples/read_s3dm.py "D:/Models/model.s3dm" --voxel 150 25 75
```

参数顺序是 **X Y Z**，索引从 0 开始。返回顺序为 **Vp、Vs、rho**；只读取并校验该格点，不扫描整个体。Python 的 `Path` 可处理中文路径。

### 6.3 扫描全部材料并输出范围

```powershell
python docs/examples/read_s3dm.py "D:/Models/model.s3dm" --scan
```

按每块 65,536 格读取，每次载荷块为 786,432 字节（0.75 MiB），避免一次性装入整个模型。扫描所有格点后，输出 Vp/Vs/rho 的最小值和最大值，并将 `payloadValidated3D` 设为 true。纯 Python 扫描大模型会花费时间，此命令不是后台任务。

默认读取节点数上限为 36,000,000，防止错误文件头导致不合理读取。外部分析若有明确需要，可用 `--max-nodes` 改变示例读取器的限制；这不会改变 SeisWave Lab 自身的网格上限。

### 6.4 在其他 Python 代码中调用

将 `read_s3dm.py` 放到调用脚本旁边：

```python
from read_s3dm import readModelHeader3D, readVoxel3D, scanModel3D

modelPath3D = "D:/Models/model.s3dm"
header3D = readModelHeader3D(modelPath3D)
vp3D, vs3D, rho3D = readVoxel3D(modelPath3D, 10, 5, 20)
print(header3D.nx3D, header3D.ny3D, header3D.nz3D)
print(vp3D, vs3D, rho3D)
# 按需执行完整扫描：ranges3D = scanModel3D(modelPath3D)
```

### 6.5 可选：NumPy 内存映射

若已经安装 NumPy，可在校验文件头后使用只读映射，避免复制完整体：

```python
import numpy as np
from read_s3dm import readModelHeader3D

modelPath3D = "D:/Models/model.s3dm"
header3D = readModelHeader3D(modelPath3D)
materials3D = np.memmap(
    modelPath3D, dtype="<f4", mode="r", offset=36,
    shape=(header3D.nz3D, header3D.ny3D, header3D.nx3D, 3), order="C",
)
vp3D = materials3D[..., 0]   # [z,y,x]，视图
vs3D = materials3D[..., 1]
rho3D = materials3D[..., 2]
xzVp3D = vp3D[:, header3D.ny3D // 2, :]  # 固定 Y 的 X-Z 剖面
yzVp3D = vp3D[:, :, header3D.nx3D // 2]  # 固定 X 的 Y-Z 剖面
xyVp3D = vp3D[header3D.nz3D // 2, :, :]  # 固定 Z 的 X-Y 剖面
```

`np.memmap` 不自动验证材料有效性。`np.array(materials3D)` 会复制完整体；不需要全体副本时保持映射和切片视图即可。

## 7. C++17 读取方法

复制 `S3dmReader3D.h` 到外部程序可访问的目录，包含它即可。所有示例类型均在 `namespace Seis3D` 内，不包含项目运行时头文件。

```cpp
#include "S3dmReader3D.h"
#include <iostream>

// Read a physical cell and stream the complete volume one XY plane at a time.
int main() {
    try {
        const std::filesystem::path modelPath3D = L"D:/Models/model.s3dm";
        Seis3D::S3dmReader3D reader3D(modelPath3D);
        const auto& header3D = reader3D.header3D();
        const auto material3D = reader3D.readVoxel3D(
            header3D.nx3D / 2, header3D.ny3D / 2, header3D.nz3D / 2);
        std::cout << "Vp=" << material3D[0] << " Vs=" << material3D[1]
                  << " rho=" << material3D[2] << '\n';

        float maximumVp3D = 0;
        for (std::uint32_t z3D = 0; z3D < header3D.nz3D; ++z3D) {
            const auto plane3D = reader3D.readPlane3D(z3D);
            for (std::size_t i3D = 0; i3D < plane3D.size(); i3D += 3)
                maximumVp3D = (std::max)(maximumVp3D, plane3D[i3D]);
        }
        std::cout << "Maximum Vp=" << maximumVp3D << '\n';
    } catch (const std::exception& error3D) {
        std::cerr << error3D.what() << '\n';
        return 1;
    }
    return 0;
}
```

在 Windows 使用宽字符串构造 `std::filesystem::path`，可直接填写中文路径。外部工程启用 C++17 或更新版本；若编译源码含中文，MSVC 使用 UTF-8 源文件设置。此头文件是文档示例，不需要加入主程序目标或单独编译。

`readPlane3D(z)` 返回 `nx×ny×3` 个 float，索引公式为：

```cpp
const auto index3D = (std::size_t(y3D) * header3D.nx3D + x3D) * 3;
const float vp3D  = plane3D[index3D];
const float vs3D  = plane3D[index3D + 1];
const float rho3D = plane3D[index3D + 2];
```

读取器构造函数不扫描全部材料；`readVoxel3D` 和 `readPlane3D` 默认校验读出的材料。只有遍历全部平面才能声称完成整个材料体的检查。`validate3D=false` 可跳过材料值检查，但仍保留文件头、大小、索引和截断检查。同一 C++ 读取器实例持有可变文件位置，不要无锁跨线程共享。

## 8. 文件解析与程序可导入不是同一层检查

示例读取器检查魔数、版本、正尺寸、节点总量、间距、边界代码和精确文件大小。逐点或扫描读取时，再检查：

```text
所有材料值有限
0 < Vp <= 30000 m/s
0 <= Vs < Vp
Vp² > (4/3) * Vs²      # 体积模量为正
0 < rho <= 50000 kg/m³
```

Vs=0 通过当前材料检查；这不构成对液固界面离散精度的验证。Python 使用双精度计算校验表达式，C++/求解器使用 float32；极接近物性阈值的输入可能出现舍入判断差异，应避免构造临界材料，并以程序实际导入结果为准。

程序还会额外检查 **各轴至少16点、CPML参数8–40格、总节点不超过3600万、4 GiB沙盒预算、显卡实际可用显存与纹理限制、频率/CFL/时步**。这些是当前应用的运行限制，不是从文件长度即可证明的性质。示例能够解析文件，不保证当前设备一定可以初始化它。

文件没有校验和。长度正确、材料有效也不能证明文件与原导出内容逐字节一致；需要论文复现时，另行保存文件 SHA-256 和实验参数。

## 9. 外部模型接入建议

当前模型页面另提供标准 **VTK ImageData（.vti）导出**，可以将已初始化的物理网格和 Vp/Vs/密度交给外部查看器。操作、坐标和数组规则见 [VTI 模型导出说明](VTK_Model_Export.md)。S3DM 仍用于当前模型导入/导出，两个格式的用途和字段不应混淆。

从外部数据转换时，应先确定物理坐标、轴顺序和单位，再生成 `[z,y,x,Vp/Vs/rho]` 的小端 float32 载荷与36字节头。不要将 `.sgy`、文本、NumPy或其他二进制文件直接改名为 `.s3dm`。

当前只支持三轴间距相同的规则、各向同性 Vp/Vs/rho 网格；非等距、地形、各向异性、Q衰减或投影坐标信息没有对应字段，需要预处理或另行设计版本。模型文件与地震记录用途不同：`.s3dm` 描述介质，项目输出的 `.sgy` 描述接收点的时间序列。

将模型提供给论文读者时，建议一并提供：本说明、读取代码、单位与轴约定、模型来源与许可、文件摘要，以及独立的震源/接收/时步实验参数表。
