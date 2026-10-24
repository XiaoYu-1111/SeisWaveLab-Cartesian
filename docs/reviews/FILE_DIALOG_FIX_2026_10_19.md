# Windows 文件选择器修复（2026-10-19）

二维 VTI 选择器此前将完整路径直接放入 `lpstrFile`，没有处理程序旁 `data_Seis2D` 等默认目录不存在的情况。现在共用 `include/SeisCommon/NativeFileDialog.h`，将有效起始目录与文件名分开传给 Windows；缺失目录逐级回退，空路径与非法文件名使用默认名称，相对路径以 EXE 目录为基准，保留 Unicode 字符。仅确认选择后才更新输入，取消保留原值。

Windows 原生打开对话框实测可能改变进程工作目录，即使设置 OFN_NOCHANGEDIR。本实现于调用前保存、结束后显式恢复，避免资源与相对路径被改变；系统错误显示具体十六进制代码。

应用入口包括二维 VTI 输入/输出、二维炮集包命名，以及三维模型、快照、SEG-Y 输出目标。文件夹导出选择入口继续使用原有 COM 文件夹选择器。

验证分为路径准备与实际系统窗口：`NativeFileDialogRegression` 检查缺失多级目录、中文目录/文件名、目录输入、空路径、相对路径、非法名称与无效盘符；`--native-dialogs` 对七种输入分别打开 Open 和 Save，等初始化完成后自动取消，合计 14 组，并检查工作目录恢复。该原生窗口测试验证弹出和取消，不模拟手工确认选中。三维工作区回归通过。

```powershell
# 从外层工作区运行；实际窗口检查需要交互式 Windows 桌面。
ctest --test-dir out/build/x64-Release -C RelWithDebInfo -R 'NativeFileDialogRegression|Seis3DWorkspaceRegression' --output-on-failure
./out/bin/RelWithDebInfo/SeisNativeFileDialogRegression.exe --native-dialogs
```

主程序仍只生成外层 `out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`。本次未改动波场计算、VTI 物性格式或正式性能数据。
