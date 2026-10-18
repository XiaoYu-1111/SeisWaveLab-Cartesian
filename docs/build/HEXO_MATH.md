# Hexo / NexT：文章资源与数学公式

修订日期：2026-10-18。配置方式参考同机独立曲网格项目；本发布包不修改博客站点，也不自动部署。

## 1. 文件放置

将发布包 `hexo` 目录中的 `source` 合并到博客根目录：

```text
source/_posts/
├─ CUDA_Seis3D.md
└─ CUDA_Seis3D/
   ├─ seis3d-01-home.png
   ├─ seis3d-09-style-coolwarm.png
   ├─ seis2d-wavefield-paper.png
   ├─ Seis3D_User_Guide.md
   └─ 其他图片、格式说明及示例
```

在站点 `_config.yml` 合并 `post_asset_folder: true` 和下面的 `skip_render`。文章使用同名资源文件夹的相对图片名；Markdown-it 的 `images.post_asset: true` 将图片解析到文章地址下，支持部署到站点子路径。保持文章文件与资源目录同名。

资源目录内的配套 Markdown 是下载附件，必须通过 `skip_render` 保持原文件，否则 Hexo 会把其中的说明书也识别为新文章。此规则仅匹配 `CUDA_Seis3D/` 内部，不匹配外面的正文 `CUDA_Seis3D.md`。已有 `skip_render` 列表应追加该规则。

附带用户指南是可下载原始 Markdown，点击不会自动获得 Hexo 独立文章页；博客正文自身已包含理论、界面步骤和截图。配套指南在本地资源目录也能用 Markdown 阅读器查看。

## 2. 公式渲染

旧 `hexo-renderer-kramed` 可能输出 MathJax 2 的 `script[type="math/tex"]`；仅开启文章 `mathjax: true` 不保证 MathJax 3 能识别这类输出。这里使用 Markdown-it 加构建时 MathJax SVG，公式在生成阶段完成排版。采用一套 Markdown 渲染器和一套数学引擎。

参考项目使用 Hexo 8.1.1、NexT 8.27.0、Node.js 22.15.0。先备份站点配置和依赖锁文件，再按已有环境合并；不要无条件覆盖站点配置。

```powershell
npm uninstall hexo-renderer-kramed
npm install --save-exact hexo-renderer-markdown-it@7.1.1 markdown-it-mathjax3@5.2.0
```

合并 `_config.yml`：

```yaml
post_asset_folder: true
skip_render:
  - "_posts/CUDA_Seis3D/**"
markdown:
  render:
    html: true
    breaks: true
    linkify: true
  anchors:
    level: 1
  images:
    prepend_root: true
    post_asset: true
  plugins:
    - name: markdown-it-mathjax3
```

在博客根目录 `_config.next.yml` 关闭重复的前端数学处理：

```yaml
math:
  every_page: false
  mathjax:
    enable: false
  katex:
    enable: false
```

文章可保留 `mathjax: true`。原始 Markdown 继续使用 `$...$`、独立行 `$$...$$`、`aligned`、`pmatrix` 和显式 `\tag{}` 编号，不要把 LaTeX 转成图片或对反斜线做全局替换。

```powershell
npx hexo clean
npx hexo generate
npx hexo server
```

在预览中检查行内下标、矩阵、多行公式、19 个编号显示公式、表格、代码块和原图链接。HTML 应有 `mjx-container`，不应有 `merror`、`data-mjx-error` 或旧 `math/tex` 标签。本次包的独立渲染检查结果见随包 `package-validation.json`；它不代表已部署网站通过检查。

## 3. 图片清晰度

41 张 PNG 均为程序原生 2560×1494 完整客户区；打包只复制字节。保留原图，主题可按容器宽度显示，点击应能查看原尺寸。不把缩略图替换为正文唯一资源。若部署后图片缺失，核对文章 slug、同名资源目录、`post_asset_folder` 和子路径配置。

## 4. 资料

- [Hexo 官方 Markdown-it 渲染器](https://github.com/hexojs/hexo-renderer-markdown-it)
- [Markdown-it MathJax 3 插件](https://github.com/tani/markdown-it-mathjax3)
- [NexT 数学公式文档](https://theme-next.js.org/docs/third-party-services/math-equations)

本页说明的是发布包配置约定；已有站点若使用兼容的 KaTeX 或另一套数学方案，可保留已验证配置，避免叠加引擎。
