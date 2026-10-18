# 文档工具

从内层 Git 仓库执行；生成结果进入父目录 `out`。

| 工具 | 用途 |
| --- | --- |
| `prepare_paper_screenshots.py` | 发布三维完整 33 帧；检查统一尺寸、同一色带比较时刻，保留 PNG 字节与摘要 |
| `prepare_seis2d_screenshots.py` | 发布二维完整 8 帧，保留尺寸、日期与摘要 |
| `package_documentation.py` | 从统一主文、指南和图片生成离线包及 Hexo 文章资源树，检查本地链接和图片摘要 |
| `package_showcase.py` | 兼容旧命令，调用上述完整打包流程 |
| `render_publication.mjs` | 使用指定现有 Node 模块目录渲染 MathJax HTML 并记录公式与图片检查 |
| `export_user_guide.py` | 既有 Word 导出；需要另行渲染检查，不属于本轮 Hexo 输出 |
| `verify_shot_package.py` | 独立检查二维单炮包文件摘要与结构 |

Python 发布工具需要 Pillow。先从外层运行程序的 `--capture-seis3d-workflow` 和 `--capture-seis2d-workflow`；随后运行两个 prepare 脚本，再运行 package。主文维护于 `docs/PROJECT_MANUAL.md`，不要只编辑生成目录中的文章。

本轮发布日期与检查条件在打包工具中显式记录。未来重新采集时更新修订日期与算例，不能将旧图冒充新采集。公式 HTML 检查可传入已安装的 `markdown-it` 与 `markdown-it-mathjax3` 所在 `node_modules`，不会安装依赖或修改博客。
