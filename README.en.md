# SeisWave Lab · Cartesian

A 2D / 3D Cartesian elastic-wave simulation workbench.

<img src="resources/icons/SeisWaveLab.png" alt="SeisWave Lab Cartesian application icon" width="96" height="96">

[中文说明](README.md) · [Contributing](CONTRIBUTING.md) · v2.0.0

Independent 2D X–Z P–SV and 3D isotropic elastic-wave workspaces share device detection, themes, language and record visualization. Both use staggered-grid velocity–stress finite differences with CPU/OpenMP and CUDA Float32 backends. The interface uses Dear ImGui Docking, ImPlot and OpenGL 4.3, with the same theme family as the separate Curvilinear application.

**Documentation:** [Theory and workflow](docs/PROJECT_MANUAL.md) · [2D guide](docs/user/Seis2D_User_Guide.md) · [3D guide](docs/user/Seis3D_User_Guide.md) · [Architecture](docs/development/CODE_ARCHITECTURE.md) · [Building](docs/build/BUILDING.md) · [Index](docs/README.md). Detailed guides currently use Chinese.

**Measurements:** [CPU/CUDA report, charts and raw data](docs/performance/CPU_CUDA_REPORT.md), 2026-10-19; 32 CPU threads, six model sizes and five repeats per configuration. See [reproduction instructions](benchmarks/README.md).

![Cartesian 2D and 3D application home](docs/assets/seis3d/full/01-home.png)

The 2D workspace provides physical model editing, external absorbing layers, source interaction, Vx/Vz receiver acquisition, SEG-Y property-model import and shot-package export. Controls and current/draft parameters are docked on the left. The 3D workspace provides orthogonal slices, a volume view, CPML/free-surface/reflecting boundaries, Vx/Vy/Vz records, S3DM models and VTI export. Its normal defaults are 300×50×300 physical nodes, 500 receiver positions and 1000 samples per shot; screenshots use a separate 200³, 150-receiver demonstration.

![Current 2D wavefield and parameter panel](docs/assets/seis2d/wavefield-paper.png)

## Simulation and display

- Five 2D and nine 3D fields, eighth-order interior staggered spatial differences and second-order time stepping; free-surface closure has its own accuracy limits.
- Configurable CPU/OpenMP threads and CUDA selection. Automatic mode prefers CUDA and falls back to CPU on device, driver or interop failure. Applying a backend change rebuilds the session.
- External 2D PML does not consume physical model cells; B toggles its visibility. 2D property import uses Vp, Vs and density SEG-Y files.
- Independent material/wavefield palettes, color scales, zoom and pan. 2D color gain defaults to 100 with a 0–1000 range.
- All-trace, selected-range and single-trace record views with RMS AGC. Display processing does not change raw exported samples.
- Chinese/English interface, shared theme family, dock layouts, grouped expand/collapse controls, current/draft parameter comparison and performance panels.

![3D slices, volume and receiver workbench](docs/assets/seis3d/full/08-wavefield.png)

The Cartesian project uses a flat surface and a regular grid. Curvilinear geometry and its numerical method belong to a separate project. Backend agreement, finite-case stability and physical accuracy have different evidence requirements; see [validation](docs/development/BACKEND_VALIDATION.md) and [the manual](docs/PROJECT_MANUAL.md).

## Repository and output directories

This folder is the Git repository. It contains CMake helpers, `src/SeisCommon`, `src/Seis2D`, `src/Seis3D`, public headers, resources, documentation images, benchmarks, tests and licenses. All generated files remain in its sibling `../out`:

```text
SeisWaveLab_Cartesian_Workspace/
├─ SeisWaveLab_Cartesian/       Git repository
│  ├─ src/ include/ cmake/      Source and build definitions
│  ├─ resources/icons/          Original PNG and ICO
│  ├─ docs/assets/              Native documentation captures
│  ├─ docs/performance/         Archived measurements
│  ├─ benchmarks/ tests/        Reproduction and regression tools
│  └─ LICENSE  THIRD_PARTY_NOTICES.md
└─ out/
   ├─ build/x64-Release/        Outer RelWithDebInfo build
   ├─ build/standalone/         Separate repository build cache
   ├─ bin/RelWithDebInfo/       Application and runtime resources
   ├─ screenshots/ benchmarks/ Local capture and benchmark output
   └─ documentation/ blog/     Generated publication bundles
```

Source images, icons and archived measurements belong in Git; EXE/DLL, build caches and simulation output remain outside it. See [layout](docs/development/REPOSITORY_LAYOUT.md). The CMake application target is `SeisWaveLab`; its sole main executable name is `SeisWaveLab_Cartesian.exe`.

## Build and run

Use MSVC x64, CMake 3.26+, Ninja, C++20 and CUDA Toolkit (12.8 tested, CUDA C++17). GLFW, GLEW and GLM are external SDK dependencies. CPU fallback works at runtime; the current source configuration still requires the CUDA toolkit. Display requires OpenGL 4.3. From this repository in a developer terminal:

```powershell
cmake -S . -B ../out/build/standalone/RelWithDebInfo -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIB_ROOT=D:/CPP_lib
cmake --build ../out/build/standalone/RelWithDebInfo --target SeisWaveLab --parallel 4
..\out\bin\RelWithDebInfo\SeisWaveLab_Cartesian.exe
```

Override SDK locations with `SEISWAVE_GLM_ROOT`, `SEISWAVE_GLEW_ROOT` and `SEISWAVE_GLFW_ROOT`. Keep outer and standalone CMake caches separate. See [build and distribution instructions](docs/build/BUILDING.md) for dependencies and checks.

## Documentation, blog and resources

On **2026-10-19**, **33 3D and 11 2D** native **2560×1494** full-window captures were refreshed from the current application. PNG bytes, capture dates, SHA-256 hashes and executable hashes are archived in `docs/assets`. Scientific fields and records come from actual CUDA computation; pictures are not cropped, recolored or synthesized. See [refresh notes](docs/reviews/DOCUMENTATION_REFRESH_2026_10_19.md).

From the outer workspace, run `SeisWaveLab_Cartesian.exe --capture-seis3d-workflow` and `--capture-seis2d-workflow`. Then, from this repository:

```powershell
python -B docs/tools/prepare_paper_screenshots.py
python -B docs/tools/prepare_seis2d_screenshots.py
python -B docs/tools/package_documentation.py
```

These tools need Pillow. The manual and categorized guides are maintained in Git; offline and Hexo bundles are generated under `../out`. Original icons are `resources/icons/SeisWaveLab.png` and `SeisWaveLab.ico`. Keep runtime DLLs, `resource_CUDA_V1`, `resource_Seis3D` and licenses beside the executable when distributing it.

## Controls and sources

In 2D, use the wheel to zoom, middle/right drag to pan and left click to fire. Enable continuous firing before holding the left button; B toggles external absorber visibility. In 3D, slices support wheel zoom and dragging, while the volume has independent rotation, zoom and pan. F1 opens the 3D shortcut help. Display gain and AGC do not alter export amplitudes.

Author: XIAO YU. See [LICENSE](LICENSE), [third-party notices](THIRD_PARTY_NOTICES.md) and [scientific scope](docs/PROJECT_MANUAL.md). The legacy 2D numerical source amplitude is not a calibrated SI force or seismic moment. Detailed quantitative validation remains bounded by the documented cases.
