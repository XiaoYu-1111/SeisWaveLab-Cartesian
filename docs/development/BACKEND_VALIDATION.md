# CPU/OpenMP 与 CUDA 更新验证

## VTI 模型接口补充验证（2026-10-19）

二维和三维模型主要使用 VTI ImageData，二维保留 SEG-Y 物性组交换。`SeisVtiModelRegression` 验证二维 17×1×19、三维 17×18×19 非立方网格，h=3.25 m，每节点变化的 Vp/Vs/密度、Float32 位级往返、SI 元数据与节点顺序；同时验证 ASCII Float64、大小端、UInt32/UInt64 长度块头，以及 Float32/Float64 二进制组合。

缺失/重复数组、截断、负密度、无效体积模量、非有限数、错误字节序/偏移、非 SI 单位、DTD、压缩和零间距均被拒绝；已有输出保护与失败清理通过。独立 Python 标准库读取 2D/3D 输出和真实 200³ 模型，其三项材料二进制文件为 **96,001,381 字节**。

六项 ctest 全部通过：VtiModelRegression、历史 VtsModelRegression、Seis3DWorkspaceRegression、Backend3DRegression、Seis2DRecordRegression、SolverRegression。实际二维 CPU 和 CUDA 应用均执行 VTI 导出/载入，逐节点比较密度及弹性系数，确认间距与深度轴方向；均完成 100 道×500 点采集及 SEG-Y 数据包导出。实际三维 CUDA 流程完成 VTI 200³ 模型导出，工作区回归完成 VTI 会话重建。旧 VTS 工具仅作历史回归，不提供应用模型入口。

新截图为 33 张三维、12 张二维。运行日志见外层 `out/screenshots/vti-2026-10-19`，独立格式验证摘要见 `out/checks/vti-interchange/validation.json`。本轮验证数据交换与会话集成，不新增物理精度结论。使用范围见 [VTI 模型格式](../user/VTI_Model_Format.md)。

日期：2026-10-19。配置：外层 `out/build/x64-Release`，RelWithDebInfo；主程序 `out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`。当前 CPU 和 CUDA 均使用原有 Float32 离散场；未改成曲网格项目的 Float64 算法。

| 检查 | 实际覆盖 | 结果 |
| --- | --- | --- |
| SolverRegression | 二维三种方案，48² 物理域外置 PML，CPU 1/4 线程 80 步，CUDA 波场/接收器对照、零显示增益、回放、原有边界/CFL/解析检查 | 通过 |
| Backend3DRegression | 24³、64 步、CPML/自由表面/反射三类，CPU 1/4 线程、CUDA Vz/接收器、剖面/统计、模型平面读写、重置与释放 | 通过 |
| Seis2DRecordRegression | 原始记录、导出、AGC/增益、采样时间等既有检查 | 通过 |
| Seis3DWorkspaceRegression | 记录、主题、停靠、分组展开折叠、语言和三维工作区检查 | 通过 |
| 实际二维应用 | `--capture-seis2d-paper`，CUDA 模式，B 键、网格/画布交互、面板分组折叠展开、冻结参数、采集与单炮导出 | 通过 |
| 实际二维无 CUDA 回退 | `CUDA_VISIBLE_DEVICES=-1`，同一实际应用流程，CPU/OpenMP 显示、100 道采集、导出与交互；导出 manifest 的设备为 CPU/OpenMP | 通过 |
| 实际三维 CPU 显示 | `SEISWAVE_FORCE_CPU=1` 与 `--check-seis3d-display`，96³、三个不透明剖面实际提交至界面、100 道/256 样本采集 | 通过 |
| 三维无 CUDA 后端 | `CUDA_VISIBLE_DEVICES=-1` 独立运行 Backend3DRegression，串行/OpenMP、三类边界、接收器、显示与模型平面往返 | 通过 |

CPU/CUDA Vz 对照采用相对 L2 容差 `2e-4`；CPU 串行和 OpenMP 要求结果逐元素一致。通过代表这些算例，不代表任意大网格、任意模型和真实无驱动机器均已实机测试。屏蔽设备用于执行 CUDA 检测失败分支。

程序依赖检查确认 PE 导入包含 CUDA Runtime、OpenMP 与 VC++ 运行库，不直接导入 `nvcuda.dll`。当前输出已部署 `cudart64_12.dll`、`vcomp140.dll` 与所需 VC++ 运行库。仍需操作系统支持的 OpenGL 4.3 环境。

测试日志在外层 `out/logs/backend-update-2026-10-19`，最新无 CUDA 二维截图在 `out/screenshots/application/2d/cpu-fallback-final-20261019`。可用 `SEISWAVE_CAPTURE_SESSION` 指定截图子目录（仅字母、数字、下划线和连字符），避免预览中的旧图片阻止覆盖。
