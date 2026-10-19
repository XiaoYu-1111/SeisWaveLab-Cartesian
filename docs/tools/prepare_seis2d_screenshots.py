"""Copy genuine 2D application captures into the tracked documentation folder."""
from pathlib import Path
import shutil
import hashlib
import json
from datetime import datetime
from PIL import Image


def prepare_screenshots() -> None:
    """Preserve original PNG pixels; do not synthesize or recolor scientific results."""
    root = Path(__file__).resolve().parents[2]
    source = root.parent / "out/screenshots/application/2d"
    output = root / "docs/assets/seis2d"
    captures = {
        "wavefield-paper.png": "seis2d-wavefield-paper.png",
        "record-raw.png": "seis2d-record-raw.png",
        "record-agc.png": "seis2d-record-agc.png",
        "single-vz.png": "seis2d-single-vz.png",
        "section-vx.png": "seis2d-section-vx.png",
        "docking-analysis.png": "seis2d-docking-analysis.png",
        "docking-record-focus.png": "seis2d-docking-record-focus.png",
        "export-package.png": "seis2d-export-package.png",
        "external-boundary.png": "seis2d-external-boundary.png",
        "parameter-groups-folded.png": "seis2d-parameter-groups-folded.png",
        "parameter-groups-expanded.png": "seis2d-parameter-groups-expanded.png",
    }
    # Check all inputs before replacing documentation images.
    for original in captures.values():
        path = source / original
        if not path.is_file():
            raise FileNotFoundError(f"Run SeisWaveLab_Cartesian.exe --capture-seis2d-workflow first: {path}")
        with Image.open(path) as image:
            if image.size != (2560, 1494):
                raise ValueError(f"Unexpected native capture size: {path}: {image.size}")
    output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, original in captures.items():
        with Image.open(source / original) as image:
            width, height = image.size
        shutil.copy2(source / original, output / name)
        manifest.append(dict(image=name, width=width, height=height, cropped=False,
            resampled=False, recolored=False, theme="Paper Sand", application_version="2.0.0",
            captured_date=datetime.fromtimestamp((source / original).stat().st_mtime).astimezone().date().isoformat(),
            sha256=hashlib.sha256((source / original).read_bytes()).hexdigest(),
            backend="CUDA", physical_grid=[256,160], spacing_m=1.0,
            solver_dt_ms=0.1, sample_interval_ms=0.2, receivers=100,
            source_grid=[128,130], source_frequency_hz=60,
            executable_sha256=hashlib.sha256((root.parent / "out/bin/RelWithDebInfo/SeisWaveLab_Cartesian.exe").read_bytes()).hexdigest()))
    (output / "capture-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Copied {len(captures)} original 2D captures to {output}")


if __name__ == "__main__":
    prepare_screenshots()
