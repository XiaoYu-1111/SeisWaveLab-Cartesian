# 项目命名与目录迁移

日期：2026-10-18。当前产品版本仍为 v2.0.0。

## 当前名称

| 项目 | 名称 |
| --- | --- |
| 产品 | SeisWave Lab |
| 外层工作区 | `SeisWaveLab_Cartesian_Workspace` |
| 内层 Git 仓库目录 | `SeisWaveLab_Cartesian` |
| CMake 项目 | `SeisWaveLab_Cartesian` |
| 主程序构建目标 | `SeisWaveLab` |
| 主入口源文件 | `src/SeisCommon/SeisWaveLab.cpp` |
| 程序 | `SeisWaveLab_Cartesian.exe` |
| 窗口标题 | SeisWave Lab — Cartesian Elastic Wave Simulation \| v2.0.0 |
| 物理模块 | `Seis2D`、`Seis3D` |

原外层和内层 `CMakePro_CUDA_SeisSim_V2` 分别迁移为上述工作区与仓库目录。原主程序 CMake 目标同名，现统一为 `SeisWaveLab`；内部 project 原名带 V1，现统一为 Cartesian。版本号继续独立于目录名管理。

```text
CMake_pro_CUDA/
├─ SeisWaveLab_Cartesian_Workspace/
│  ├─ AGENTS.md、CMakeLists.txt、CMakeSettings.json、cmake/
│  ├─ SeisWaveLab_Cartesian/        Git 根目录，包含原 .git 历史
│  └─ out/                        所有编译和生成输出
└─ Seis3D_Curvilinear/             独立曲网格项目
```

## 在 Visual Studio 继续工作

关闭旧路径的解决方案／文件夹，从新路径打开外层 `SeisWaveLab_Cartesian_Workspace`。也可只打开内层 Git 仓库，后者使用独立 standalone 缓存。VS 最近打开列表可能仍保留旧路径，应选择新目录；无需重新 git init 或克隆。

旧 CMake 缓存写有旧源码绝对路径，迁移时归档到 `out/archive/project-rename-2026-10-18/build`；旧 `.vs` 缓存也归入同处的专用目录。在新目录重新配置生成缓存，不通过文本替换旧缓存续用。

外层发布配置为 RelWithDebInfo，构建目录 `out/build/x64-Release`，程序 `out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`；Debug 同理为 `out/build/x64-Debug` 与 `out/bin/Debug`。独立内层构建继续使用外层 `out/build/standalone/<配置>`。

```powershell
cmake --build out/build/x64-Release --target SeisWaveLab --parallel 4
```

现有数据、录像、历史截图和用户偏好复制到新目录。Windows 拒绝原地目录重命名，因此本轮采用校验复制迁移，原路径暂保留为迁移前副本；后续只维护新路径。旧截图内可见的本机路径属于拍摄时信息，不能以图像编辑伪造新路径；后续需要时重新截图。历史审查保留原项目名及原日期。

## Git 与网络仓库

本次仅改变本机目录、CMake 和产品描述，原样复制 `.git` 并比对全部仓库文件，不改分支或提交历史。原 GitHub origin 地址仍是实际仓库地址，外部文档链接也保持；本机重命名不会自动重命名 GitHub 仓库。没有自动提交、推送或发布。
