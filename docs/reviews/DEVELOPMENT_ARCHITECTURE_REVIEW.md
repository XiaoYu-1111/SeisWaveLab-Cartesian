# Core workflow review and 3D upgrade plan

Historical scope: this review predates the isolated Seis3D implementation. For the current 3D workspace and solver, see [the implementation guide](../development/SEIS3D_IMPLEMENTATION.md) and [the illustrated user guide](../user/Seis3D_User_Guide.md).

Update (2026-10-12): see [the numerical and stability follow-up](NUMERICAL_STABILITY_REVIEW.md) for implemented fixes and executed CUDA regression/memcheck results. The initial findings below describe the state before that follow-up; CUDA error propagation, repeated release, stencil weights, receiver safety, and lazy legacy initialization have since been addressed.

Review scope: application-owned C++/CUDA code in `Main_header`, `Main_cpp`, and `CUDA_Kernels`. Dear ImGui, ImPlot, and stb sources are third-party dependencies and are intentionally excluded from project-wide renaming.

## Current workflow

1. `main` probes the CUDA device, creates the GLFW/OpenGL context, initializes UI resources, and enters the frame loop.
2. The simulation workspace lazily creates the 2D simulation context, material fields, CUDA buffers, and CUDA-OpenGL wavefield texture.
3. Each running frame advances one or more time steps. The host coordinates source injection, receiver sampling, replay capture, automatic acquisition, and presentation.
4. Results are copied to host memory for receiver traces and replay; SEG-Y/CSV and raw replay data are exported by separate I/O helpers.

This is a workable single-process 2D workflow. The simulation state, UI state, file I/O, CUDA allocation, and solver selection are still tightly coupled through globals and large headers, so adding another dimensionality directly to the current types would spread conditionals across the UI and kernels.

## Findings to address

### High priority

- `CUDA_CHECK` in `include/Seis2D/Cuda_Check.cuh` prints an error and continues. Allocation or copy failures can therefore leave null/invalid device buffers that later kernels still use. Change CUDA-facing functions to return a structured status (or throw and catch at a deliberate application boundary), and check kernel launch errors after launches.
- `SimulationContext` and `GPUSimData` represent a 2D grid with `NX`, `NZ`, `total_grid`, and `i * NX + j` indexing. `total_grid` is an `int`. A 3D grid needs 64-bit size calculations and explicit `x/y/z` strides before dimensions grow.
- `recordReceiverStepOnGpu` allocates two host vectors and performs two blocking device-to-host copies for every sampled time step. Reuse host buffers and batch/pin transfers or use an asynchronous ring buffer before scaling receiver count and sample rate.

### Medium priority

- `setupTestContext` and the solver/UI code rely on global `ctx`, `gpu_data`, `par`, and acquisition state. This makes repeated runs and independent simulations difficult to reason about. Group solver state and resources behind an owning simulation/session object.
- Solver selection uses integer `flag_type` values (1/2/3) across host and device code. Replace these magic values with a named `SimulationScheme` enum and a single dispatch point.
- Several host helpers allocate large temporary arrays with raw `malloc` or allocate per frame. Prefer `std::vector`, reusable staging buffers, and checked size arithmetic.
- The application still initializes the legacy life-game CUDA buffers during startup although the primary home page no longer offers that screen. Make that feature lazy or remove its startup allocations once its intended lifecycle is decided.

## Naming and documentation rules

- Types and enum types: `PascalCase` (`SimulationContext`, `SimulationScheme`).
- Functions, parameters, and local/member variables: `lowerCamelCase` (`initializeGpuSimulation`, `receiverCount`).
- Constants: `kPascalCase` for compile-time constants (`kStencilRadius`).
- CUDA kernels: `lowerCamelCase` plus `Kernel` suffix (`updateType1StressKernel`).
- Avoid prefixes that duplicate the type (`p`, `ptr`) unless they communicate memory space or ownership. For device pointers, prefer a consistent `device`/`host` suffix or member grouping rather than mixed `d_` and `h_` conventions.
- Put a short API comment immediately before every non-trivial function. Explain purpose, side effects/resource ownership, and important preconditions; do not narrate the implementation line by line.
- Keep third-party code and external file-format field names unchanged. Rename application symbols atomically across declarations, definitions, and all call sites.

This pass applies lower-camel naming and function comments to CUDA APIs and kernels, replay upload, the central simulation workflow, model conversion/import/export, receiver synchronization, and acquisition export helpers. UI draw helpers and data members remain for separate compile-checked batches; the current pass does not claim a project-wide naming migration is complete.

## Recommended 3D architecture

1. Introduce `GridGeometry3D` with `nx`, `ny`, `nz`, `dx`, `dy`, `dz`, and checked `size_t` strides/indexing. Keep the current 2D solver as a supported backend rather than redefining `NZ` semantics.
2. Separate `MaterialModel`, `SourceSet`, `ReceiverSet`, `BoundaryConfig`, and `TimeGrid` from ImGui state. Make validation (CFL, dimensions, material positivity, PML thickness) a pre-run step that returns actionable errors.
3. Define a solver/backend interface for initialize, advance, sample receivers, export snapshots, and release. Implement 2D behind it first, then add 3D CUDA kernels with `vx/vy/vz` and six independent stress components.
4. Plan memory before allocating: 3D field storage grows with `nx * ny * nz`; estimate all wavefields, split-PML auxiliaries, material arrays, snapshots, and acquisition buffers against available VRAM. Add tiled or domain-decomposed execution if the complete model does not fit.
5. Separate I/O from solver state and define explicit metadata for dimension order, axis units, sample interval, component, and endianness. Preserve existing 2D SEG-Y behavior with compatibility tests before changing formats.
6. Add small deterministic numerical checks before optimization: homogeneous-medium travel time, source symmetry, free-surface reflection, PML reflection level, receiver indexing, and 2D/3D memory/index bounds.

The present code is a suitable starting point for a staged 3D upgrade, but it is not yet 3D-ready. The key prerequisite is to separate grid geometry and solver state from the current 2D globals, then validate the existing 2D backend against reference cases.
