# 生产求解器性能基准

`ComputeBenchmark.cpp` 直接调用生产二维和三维 CPU/OpenMP / CUDA 求解器，不创建 GUI。默认 32 工作线程；显式关闭 OpenMP 动态调整并核验实际线程数。进程优先级仅提升到 Above Normal，退出时结束，不改系统电源方案。

在已加载 VS x64 编译环境的外层工作区执行：

```powershell
cmake -S . -B out/build/x64-Release -DSEISWAVE_BUILD_BENCHMARKS=ON
cmake --build out/build/x64-Release --target SeisComputeBenchmark -j 4
& ./out/bin/RelWithDebInfo/SeisComputeBenchmark.exe --output out/benchmarks/retest/formal-2d --threads 32 --repeats 5 --warmup 20 --steps 100 --dimension 2 --ftz 0
& ./out/bin/RelWithDebInfo/SeisComputeBenchmark.exe --output out/benchmarks/retest/formal-3d --threads 32 --repeats 5 --warmup 20 --steps 40 --dimension 3 --ftz 0
powercfg /getactivescheme | Set-Content out/benchmarks/retest/power-plan.txt
```

默认二维尺寸为 256² / 1024² / 2048²，三维为 48³ / 96³ / 160³。`--size N` 可以单独运行某尺寸；`--repeats` 应为不小于 3 的奇数。每个尺寸测 CPU、CUDA 逐步同步和 CUDA 每 8 步同步，各轮从零场重新预热。物性、间距、dt、边界和计时范围见正式报告及输出 `hardware.json`。

正式输出为 `hardware.json`、`trials.csv`、`summary.csv`、`COMPLETE.txt`。必须存在 COMPLETE 才代表计时与全物理域 Vz 校验全部完成；CUDA 两种批次的波场也被对照。CPU/CUDA 计时不能与其它重负荷计算同时运行。初始化、清场、显示、接收器采样和导出不计时。

`--ftz 1` 只用于单独测试 CPU FTZ/DAZ（次正规数清零）模式；正式主表使用默认环境对照，不更改模拟程序全局浮点设置。该可选模式在全部 OpenMP 线程设置 MXCSR，并将前后值写入硬件 JSON。

`RenderReport.py` 从正式 CSV 重新计算中位数并核验 90 个区块，输出图、报告和可归档数据。脚本需要 Python、NumPy、Matplotlib；图使用本机字体渲染，不复制字体文件。遥测可用 nvidia-smi 每 2 秒采样一次并保存成 `formal-2d/gpu-telemetry.csv`、`formal-3d/gpu-telemetry.csv`。

```powershell
python SeisWaveLab_Cartesian/benchmarks/RenderReport.py --results out/benchmarks/cpu-cuda-2026-10-19 --publish SeisWaveLab_Cartesian/docs/performance
```

当前报告：[CPU/CUDA 性能测试](../docs/performance/CPU_CUDA_REPORT.md)。所有运行产物和缓存统一置于外层 out，正式报告和复现数据归档到源码仓库 docs/performance。

也可运行 `./SeisWaveLab_Cartesian/benchmarks/RunSuite.ps1 -Threads 32 -OutputRoot out/benchmarks/retest` 自动串行执行六档测试并采集 GPU 遥测。请使用新的输出目录，避免覆盖已有正式证据；该脚本不编译程序、不更改系统电源设置，退出时仅清理自己启动的测试进程。
