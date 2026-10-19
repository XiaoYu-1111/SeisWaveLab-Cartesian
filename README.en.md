# SeisWave Lab

[中文说明](README.md) · [Documentation](docs/README.md) · [Publication roadmap](docs/publication/PUBLICATION_ROADMAP.md)

The project's custom S3DM model format is documented in [S3DM format and standalone readers](docs/user/S3DM_Model_Format.md), with GPU-independent [Python](docs/examples/read_s3dm.py) and [C++17](docs/examples/S3dmReader3D.h) examples.

SeisWave Lab v2.0.0 is an interactive Windows desktop workspace for two-dimensional and three-dimensional elastic-wave simulation and seismic-record inspection. It combines CUDA finite differences, OpenGL 4.3 visualization, Dear ImGui Docking and ImPlot. The Seis3D module owns its own simulation state and GPU/graphics resources; switching modes releases the previous module's GPU resources.

## Features

- Isotropic velocity–stress solvers with high-order staggered spatial derivatives and explicit time stepping.
- Independent 2D docked controls, wavefield and record viewer, with modeling, comparison and record-focus layouts.
- 3D orthogonal slices and volume visualization, synthetic model presets, configurable boundaries and memory checks.
- VTK XML ImageData (.vti) export of full-resolution models or the currently selected wavefield snapshot, with physical units and time metadata; see the [export guide](docs/user/VTK_Model_Export.md).
- Receiver acquisition, fixed-length SEG-Y import, single/multiple traces and adjustable RMS AGC for display.
- Display gain and palette settings preserved across data/component changes. Record export uses raw samples.
- 2D shot packages with both velocity components, geometry, optional frozen model/source arrays and a SHA-256 manifest.
- English/Chinese interface and light scientific themes.

Seis3D starts with Vz, the Paper scientific visual preset, Scientific White materials and Coolwarm wave colors at 1.8x display gain. The default 300 x 50 x 300 grid uses 100 X receivers in 5 Y rows (500 total, also the acquisition limit), 1000 samples per shot and a single-shot source spread (1 x 1). Switching visual presets preserves tuned display parameters; interface themes are selected independently.

## Build on Windows

The validated environment uses MSVC x64, CMake 3.26 or newer (3.31.6-msvc6 used locally), Ninja, CUDA Toolkit 12.8 and an NVIDIA GPU supporting OpenGL 4.3/CUDA interoperability. Host C++ uses C++20; CUDA sources use C++17. GLFW 3.4, GLEW 2.1.0 and GLM are external SDK dependencies, not bundled package-manager installations.

After initializing the MSVC x64 environment, run from this repository:

```powershell
cmake -S . -B ../out/build/standalone/RelWithDebInfo -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIB_ROOT=D:/CPP_lib
cmake --build ../out/build/standalone/RelWithDebInfo --target SeisWaveLab --parallel 2
```

The legacy `LIB_ROOT/OpenGL` directory layout is documented in [BUILDING](docs/build/BUILDING.md). Alternatively set `SEISWAVE_GLM_ROOT`, `SEISWAVE_GLEW_ROOT` and `SEISWAVE_GLFW_ROOT` individually. CMake discovers CUDA; an explicit `CUDAToolkit_ROOT` and `CMAKE_CUDA_COMPILER` may select an installation. `CMAKE_CUDA_ARCHITECTURES` defaults to `native`; specify a supported architecture list for distribution builds.

All build outputs remain outside the source repository, under `../out`. The executable is `../out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`. Keep `resource_CUDA_V1` and `resource_Seis3D` beside it. This repository now contains its own CMake helper scripts and does not need the optional parent wrapper's scripts.

## Checks and documentation

Enable `SEISSIM_BUILD_SOLVER_TESTS` for `Seis2DRecordRegression` and `SeisSimSolverRegression`. Enable `SEIS3D_BUILD_WORKSPACE_CHECKS` for `Seis3DWorkspaceRegression`. The record check is CPU-only at runtime, but configuring the current project still requires the CUDA toolchain. Numerical checks require CUDA; graphics checks also require an OpenGL context. Return code 77 denotes an unavailable device/skip where supported.

The hidden real-application command `SeisWaveLab_Cartesian.exe --capture-seis2d-paper` creates eight documentation images, a real raw-data shot package and acquisition/workspace checks. It preserves saved user theme/layout preferences. `docs/tools/verify_shot_package.py` independently checks package hashes and layouts with Python 3.11 or newer.

Detailed operational guides currently use Chinese: [2D guide](docs/user/Seis2D_User_Guide.md), [3D guide](docs/user/Seis3D_User_Guide.md). Full English tutorials and public automated CI are future work.

The [complete Chinese manual](docs/PROJECT_MANUAL.md) now covers Cartesian-grid theory, 19 numbered equations, implementation and reproducible workflows. On 2026-10-18 all 41 native 2560x1494 workflow screenshots were recaptured. Run `python -B docs/tools/package_documentation.py` for offline documentation and a Hexo post with its matching asset folder under the parent `out`. Curvilinear/topographic solvers remain in the separate Seis3D_Curvilinear project.

## Scientific scope and limitations

This is a research/teaching sandbox with focused regression checks. A complete quantitative validation suite and published performance study are still needed. High-order interior differences do not imply the same global convergence order at sources, interfaces and free surfaces. The legacy 2D source uses a numerical injection scale of 1e7; the UI amplitude is not a calibrated SI force or seismic moment.

2D SEG-Y uses its legacy Y-as-depth convention. Packages explicitly record this convention and the local acquisition time origin. Continuation packages omit the initial dynamic fields, and multiple-source events are not archived; a package is not a restart checkpoint or a complete batch-reproduction facility. AGC/visual normalization must not be used to evaluate physical amplitudes.

## License, contributions and citation

Project-authored material follows the existing [MIT license](LICENSE); dependencies and resources retain their own terms. See [third-party notes](THIRD_PARTY_NOTICES.md) and [contribution guidance](CONTRIBUTING.md). Some resource redistribution permissions still need review before a public binary release.

No paper or archived DOI is claimed. For a research result, record the actual source commit, software version, dependencies and experiment parameters. Formal citation metadata will be added once author information and a permanent release identifier are confirmed.

## Current launch entry

Use the outer `out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe`. The `SeisWaveLab` CMake target directly produces this executable as its sole application output. Production icons are in `resources/icons`.

Both 2D and 3D support CPU/OpenMP and CUDA with automatic CPU fallback. Source and public headers are grouped into Seis2D, Seis3D and SeisCommon. See [code architecture](docs/development/CODE_ARCHITECTURE.md) for backend ownership, parameter drafts, fold groups and benchmark scope.
