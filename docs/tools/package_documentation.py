"""Package Cartesian manuals, unchanged native images and Hexo assets into outer out/."""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path
from urllib.parse import quote, unquote

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
OUT = ROOT.parent / "out"
DATE = "2026-10-18"
SLUG = "CUDA_Seis3D"
TITLE = "SeisWave Lab：笛卡尔网格二维与三维弹性波模拟的理论、实现与工作流程"
ORIGIN = "https://github.com/XiaoYu-1111/CMakePro_CUDA_SeisSim_V2/blob/master/"
LINK = re.compile(r'(!?)\[([^\]]*)\]\(([^)\n]+)\)')
FENCE = re.compile(r'(^```[^\n]*\n.*?^```[^\n]*$)', re.M | re.S)
SELECTED = ["PROJECT_MANUAL.md", "README.md", "user/Seis3D_User_Guide.md",
    "user/Seis2D_User_Guide.md", "user/Seis3D_Screenshot_Gallery.md",
    "user/S3DM_Model_Format.md", "user/VTK_Model_Export.md", "build/BUILDING.md",
    "build/HEXO_MATH.md", "development/REPOSITORY_LAYOUT.md",
    "development/SEIS3D_IMPLEMENTATION.md", "development/SEIS2D_REFACTOR_AND_VIEWER.md",
    "publication/PUBLICATION_ROADMAP.md", "reviews/DOCUMENTATION_REFRESH_2026_10_18.md"]
FRONT = f'''---
title: "{TITLE}"
date: 2026-05-30 13:01:56
updated: {DATE}
tags: [Computational Geophysics, Elastic Wave, Cartesian Grid, CUDA, FDTD, Visualization]
categories: [学术研究与技术项目]
mathjax: true
toc: true
---

'''


def digest(path: Path) -> str:
    """Hash source bytes, including unmodified native PNG payloads."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def convert_links(body: str, source: Path, names: dict[Path, str], images: dict[Path, str]) -> str:
    """Rewrite actual links while leaving fenced source/LaTeX examples intact."""
    def replace(match: re.Match) -> str:
        bang, label, target = match.groups()
        if target.startswith(("https:", "http:", "#", "mailto:")):
            return match.group()
        path, sep, anchor = target.partition("#")
        original = (source.parent / unquote(path)).resolve()
        if not original.is_file():
            raise ValueError(f"Missing source link: {source}: {target}")
        dest = images.get(original) or names.get(original)
        if dest is None:
            relative = original.relative_to(ROOT).as_posix()
            dest = ORIGIN + quote(relative, safe="/")
        if sep:
            dest += "#" + anchor
        return f"{bang}[{label}]({dest})"
    return "".join(part if i % 2 else LINK.sub(replace, part)
        for i, part in enumerate(FENCE.split(body)))


def validate_local_links(folder: Path, files: set[str]) -> int:
    """Reject missing package-relative destinations, including Markdown attachments."""
    count = 0
    for name in sorted(files):
        if not name.endswith(".md"):
            continue
        page = folder / name
        for i, part in enumerate(FENCE.split(page.read_text(encoding="utf-8"))):
            if i % 2:
                continue
            for _, _, target in LINK.findall(part):
                if target.startswith(("https:", "http:", "#", "mailto:")):
                    continue
                base = page.with_suffix("") if page.name == SLUG + ".md" and page.parent.name == "_posts" else page.parent
                resolved = (base / unquote(target.split("#", 1)[0])).resolve()
                resolved.relative_to(folder.resolve())
                if not resolved.is_file():
                    raise ValueError(f"Broken packaged link: {page}: {target}")
                count += 1
    return count


def write_zip(folder: Path, destination: Path, files: set[str]) -> None:
    """Archive explicit current files, excluding stale output and simulations."""
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as stream:
        for name in sorted(files):
            stream.write(folder / name, name)


def package_documentation() -> None:
    """Generate offline documentation, Hexo assets and a traceable manifest."""
    offline = OUT / "documentation/SeisWaveLab"
    blog = OUT / "blog/SeisWaveLab"
    post = blog / f"hexo/source/_posts/{SLUG}.md"
    assets = post.with_suffix("")
    offline.mkdir(parents=True, exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    names = {(DOCS / name).resolve(): ("DOCUMENTATION_INDEX.md" if name == "README.md" else Path(name).name)
        for name in SELECTED}
    names[(DOCS / "showcase/CUDA_Seis3D_GitHub_Pages.md").resolve()] = "PROJECT_MANUAL.md"
    example_files = [ROOT / "docs/examples/read_s3dm.py", ROOT / "docs/examples/S3dmReader3D.h"]
    for p in example_files:
        names[p.resolve()] = p.name
    for name in ["LICENSE", "THIRD_PARTY_NOTICES.md"]:
        names[(ROOT / name).resolve()] = name
    images: dict[Path, str] = {}
    rows = []
    for dimension, subdir, expected in [("seis3d", "seis3d/full", 33), ("seis2d", "seis2d", 8)]:
        source = DOCS / "assets" / subdir
        records = json.loads((source / "capture-manifest.json").read_text(encoding="utf-8"))
        if len(records) != expected:
            raise ValueError(f"Incomplete current {dimension} image set")
        for row in records:
            p = (source / row["image"]).resolve()
            with Image.open(p) as native:
                if native.size != (2560, 1494):
                    raise ValueError(f"Unexpected native dimensions: {p}: {native.size}")
            if digest(p) != row["sha256"]:
                raise ValueError(f"Changed capture: {p}")
            if row.get("captured_date") != DATE:
                raise ValueError(f"Run the current capture workflow first: {p}")
            images[p] = dimension + "-" + p.name
            rows.append(dict(row, packaged_image=images[p], dimension=dimension))
    files: set[str] = set()
    for p, name in images.items():
        shutil.copy2(p, offline / name)
        files.add(name)
    for relative in SELECTED:
        p = (DOCS / relative).resolve()
        text = convert_links(p.read_text(encoding="utf-8"), p, names, images)
        (offline / names[p]).write_text(text, encoding="utf-8")
        files.add(names[p])
    for p in example_files + [ROOT / "LICENSE", ROOT / "THIRD_PARTY_NOTICES.md"]:
        shutil.copy2(p, offline / names[p.resolve()])
        files.add(names[p.resolve()])
    manifest = dict(product="SeisWave Lab", version="2.0.0", date=DATE,
        grid="Cartesian", current_screenshots=41, native_dimensions=[2560,1494],
        image_processing="Byte-identical copy; no crop, resample or recolor", captures=rows)
    (offline / "capture-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    files.add("capture-manifest.json")
    (offline / "README.md").write_text("# SeisWave Lab 图文说明\n\n"
        "[完整理论与流程](PROJECT_MANUAL.md) · [三维用户指南](Seis3D_User_Guide.md) · "
        "[二维用户指南](Seis2D_User_Guide.md) · [完整截图集](Seis3D_Screenshot_Gallery.md)\n\n"
        "2026-10-18，笛卡尔网格项目，41 张 2560×1494 原生完整窗口图。"
        "Markdown 阅读器需要支持数学公式。历史审查与旧图链接保留到源码仓库。"
        "不含模型体、地震道集或可执行文件；运行数据在外层 out/screenshots。\n", encoding="utf-8")
    files.add("README.md")
    links = validate_local_links(offline, files)
    main = DOCS / "PROJECT_MANUAL.md"
    source_body = main.read_text(encoding="utf-8")
    def repository_link(match: re.Match) -> str:
        bang,label,target = match.groups()
        if target.startswith(("https:","http:","#","mailto:")):
            return match.group()
        return f"{bang}[{label}](../{target})"
    showcase_body = "".join(part if i % 2 else LINK.sub(repository_link,part)
        for i,part in enumerate(FENCE.split(source_body)))
    (DOCS / "showcase/CUDA_Seis3D_GitHub_Pages.md").write_text(FRONT + showcase_body, encoding="utf-8")
    for name in files:
        shutil.copy2(offline / name, assets / name)
    post.write_text(FRONT + (offline / "PROJECT_MANUAL.md").read_text(encoding="utf-8"), encoding="utf-8")
    hexo_files = {f"hexo/source/_posts/{SLUG}.md"} | {f"hexo/source/_posts/{SLUG}/{name}" for name in files}
    (blog / "README.md").write_text("# SeisWave Lab：Hexo 发布包\n\n"
        "将 hexo/source 合并到博客 source。文章 CUDA_Seis3D.md 与同名资源目录必须一起复制，"
        "站点启用 post_asset_folder，并将资源目录加入 skip_render，避免附件 Markdown 被识别为新文章。"
        "数学渲染与 NexT 合并配置见资源目录 HEXO_MATH.md。\n\n"
        "文章源自仓库 docs/PROJECT_MANUAL.md，保留 2026-05-30 发布日期，更新日期为 2026-10-18。"
        "本包有当前 41 张完整原图及配套 Markdown，所有图片与仓库字节相同。"
        "不包含程序、模拟体或记录数据，不修改网站、不自动部署。"
        "历史审查链接保留到源码仓库。\n", encoding="utf-8")
    hexo_files.add("README.md")
    (blog / "hexo/_config.seiswave.example.yml").write_text('''# Merge these keys into your existing site configuration; do not replace it wholesale.
post_asset_folder: true
skip_render:
  - "_posts/CUDA_Seis3D/**"
markdown:
  render:
    html: true
    breaks: true
    linkify: true
  images:
    prepend_root: true
    post_asset: true
  plugins:
    - name: markdown-it-mathjax3
''',encoding="utf-8")
    hexo_files.add("hexo/_config.seiswave.example.yml")
    checks = dict(date=DATE, screenshots=41, local_links_checked=links,
        display_equations=len(re.findall(r"^\\tag\{\d+\}",source_body,re.M)),
        native_resolution=[2560,1494], screenshot_hashes_verified=True,
        note="Math HTML rendering is recorded separately in math-render-validation.json; no website deployed.")
    for directory in [offline,blog]:
        (directory / "package-validation.json").write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding="utf-8")
    files.add("package-validation.json");hexo_files.add("package-validation.json")
    validate_local_links(blog / "hexo/source/_posts", {SLUG+".md"} | {SLUG+"/"+f for f in files if f!="package-validation.json"})
    rendered = blog / "math-render-validation.json"
    if rendered.is_file():
        result = json.loads(rendered.read_text(encoding="utf-8"))
        if result.get("article_sha256") == digest(post) and result.get("errors") == 0:
            shutil.copy2(rendered, offline / rendered.name)
            files.add(rendered.name);hexo_files.add(rendered.name)
            checks["math_rendering"] = dict(containers=result["math_containers"],display=result["display_formulas"],errors=0)
            for directory in [offline,blog]:
                (directory / "package-validation.json").write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding="utf-8")
    generated = OUT / "docs-work/SeisWaveLab/hexo-generate-validation.json"
    if generated.is_file():
        result = json.loads(generated.read_text(encoding="utf-8"))
        if result.get("article_sha256") == digest(post) and result.get("math_errors") == 0 and result.get("posts") == 1:
            for directory in [offline,blog]:
                shutil.copy2(generated,directory / generated.name)
            files.add(generated.name);hexo_files.add(generated.name)
            checks["hexo_generate"] = result
            for directory in [offline,blog]:
                (directory / "package-validation.json").write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding="utf-8")
    files.add("publication-files.json");hexo_files.add("publication-files.json")
    (offline / "publication-files.json").write_text(json.dumps(sorted(files),ensure_ascii=False,indent=2),encoding="utf-8")
    (blog / "publication-files.json").write_text(json.dumps(sorted(hexo_files),ensure_ascii=False,indent=2),encoding="utf-8")
    write_zip(offline, offline.parent / "SeisWaveLab_Documentation.zip",files)
    write_zip(blog, blog.parent / "SeisWaveLab_Hexo.zip",hexo_files)
    print(json.dumps(checks,ensure_ascii=False))
    print(f"Offline: {offline}\nHexo: {post}\nZIP: {blog.parent / 'SeisWaveLab_Hexo.zip'}")


if __name__ == "__main__":
    package_documentation()
