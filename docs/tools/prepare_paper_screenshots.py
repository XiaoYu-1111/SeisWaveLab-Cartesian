"""Copy native full-window captures without cropping, resampling or recoloring."""
from pathlib import Path
import csv
import hashlib
import json
import shutil
from datetime import datetime
from PIL import Image

TITLES = ["中文首页", "模型与网格", "安全时步", "震源设置", "模型文件", "接收排列", "采集参数",
    "运行波场", "Coolwarm", "Seismic Bipolar", "Viridis", "Turbo", "Inferno", "CUDA Green",
    "速度模", "体绘制参数", "边界层显示", "采集中", "全部150道", "中间50道", "AGC对比",
    "密度图", "单道三分量", "导出记录", "单炮导入", "AGC控件", "快捷键帮助", "断层模型",
    "英文首页", "内置操作手册", "自由顶面", "六面吸收", "六面反射"]


def prepare_screenshots() -> None:
    """Publish identical PNG bytes and retain the actual physical/rendering conditions."""
    root = Path(__file__).resolve().parents[2]
    source = root.parent / "out/screenshots/seis3d-full"
    output = root / "docs/assets/seis3d/full"
    output.mkdir(parents=True, exist_ok=True)
    with (source / "capture-manifest.csv").open(encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != len(TITLES):
        raise ValueError("The complete 33-frame workflow has not finished")
    captured_date = datetime.fromtimestamp((source / "capture-manifest.csv").stat().st_mtime).astimezone().date().isoformat()
    gallery = ["# Seis3D 完整窗口截图集", "", f"更新于 {captured_date}。所有截图来自 SeisWave Lab v2.0.0 实际运行，原始分辨率为 2560×1494，不裁切、不重采样、不重新着色。文字倍率为1.0，剖面均为100%适配。", "",
        "本次展示使用200³双层模型、10 m间距、20 Hz Ricker震源、0.5 ms手动安全步长与50×3接收排列。软件默认网格仍为300×50×300，默认步长仍为自动模式。色带比较统一暂停在第480步（0.240 s）；Vz深度切片移到Z=70，避免位于震源对称面。记录分析使用软件自身的Receiver focus布局，图片没有做外部放大。", "",
        "正式采集为150道、每道1000样点、间隔1 ms，首末采样时间为0.75～999.75 ms。模型、SEG-Y和几何文件保存在外层out/screenshots的独立会话目录。边界模式图片展示实际初始化设置，不作为边界反射性能测试。", ""]
    for index, (row, title) in enumerate(zip(rows, TITLES), 1):
        image_path = source / row["image"]
        with Image.open(image_path) as image:
            if image.size != (2560, 1494):
                raise ValueError(f"Unexpected native capture size: {image_path}: {image.size}")
        if 9 <= index <= 14 and int(row["step"]) != 480:
            raise ValueError("Palette comparisons must use one paused wavefield")
        shutil.copy2(image_path, output / image_path.name)
        row.update(title=title, captured_date=captured_date,
            sha256=hashlib.sha256(image_path.read_bytes()).hexdigest(), cropped=False, resampled=False,
            recolored=False, theme="Paper Sand", application_version="2.0.0", backend="CUDA",
            executable_sha256=digest_executable(root))
        gallery.extend([f"## {index:02d} {title}", "", f"![{title}](../assets/seis3d/full/{image_path.name})", "",
            f"图{index}　{title}。完整程序窗口，2560×1494。", ""])
    (output / "capture-manifest.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
    (root / "docs/user/Seis3D_Screenshot_Gallery.md").write_text("\n".join(gallery), encoding="utf-8")
    print(f"Copied {len(rows)} native full-window PNGs without image transforms")


def digest_executable(root: Path) -> str:
    return hashlib.sha256((root.parent / "out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe").read_bytes()).hexdigest()


if __name__ == "__main__":
    prepare_screenshots()
