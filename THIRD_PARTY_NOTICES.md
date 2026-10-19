# Third-party components and release audit

The repository's MIT license covers project-authored material under the existing copyright notice. It does not relicense third-party libraries, fonts, icons or runtime components. This inventory is a release-preparation checklist, not a claim that all redistribution permissions have been cleared.

| Component | Current integration | Attribution / remaining action |
| --- | --- | --- |
| Dear ImGui Docking | Vendored `ImGui_docking`; header reports 1.92.6 WIP | MIT upstream; the current copy refers to LICENSE.txt but that file is absent. Restore the matching upstream license and record the actual version/commit before release. |
| ImPlot | Vendored `ImPlot`; 0.18 WIP | MIT notice is embedded in `implot.h`, crediting Evan Pezent and Breno Cunha Queiroz; preserve it and determine the vendored commit. |
| stb image utilities | Vendored headers in `stb_image` | Preserve the complete MIT/public-domain alternative text in the headers; record the exact versions used. |
| GLFW | External Windows SDK, local 3.4 | Preserve its upstream license and notices in any distributed binary package; SDK is not committed here. |
| GLEW | External Windows SDK, local 2.1.0 | Multiple upstream notices apply; copy the SDK's actual license notices when packaging. |
| GLM | External headers | Record the actual version and license notices; do not infer a commit from the directory name `glm-master`. |
| NVIDIA CUDA Runtime | Toolkit 12.8; cudart64_12.dll deployed beside the EXE | NVIDIA proprietary redistributable runtime terms apply; the application loads the driver through this runtime when available. |
| Microsoft VC++ / OpenMP Runtime | Compiler redistributable runtime (vcomp140 and VC++ DLLs) | Microsoft Visual Studio redistributable runtime terms apply. Files are copied from the compiler redistributable set. |
| Windows SDK / OpenGL system libraries | Platform dependencies | System/runtime distribution rules apply; do not copy development SDK libraries as application assets. |
| Fonts in `resource_CUDA_V1/font` | Runtime resources | Audit every file and origin. In particular, a locally installed font such as Consolas is not automatically licensed for application redistribution. Replace with an appropriately licensed alternative or obtain permission. |
| Icons and other resource images | Runtime resources | Confirm origin, authorship and redistribution rights; retain original attribution where required. |

The exact dependency tree, complete English comments/documentation and third-party license bundle remain publication-release tasks. This iteration copies the existing project MIT license into the Git repository without changing its copyright owner.
