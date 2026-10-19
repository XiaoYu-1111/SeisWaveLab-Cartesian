# CPU/OpenMP 与 CUDA 更新验证

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
