"""Read SeisWave Lab S3DM v1/v2 without CUDA, OpenGL or third-party packages.

Python >= 3.10. See ../user/S3DM_Model_Format.md for the on-disk layout.
Header inspection does not validate the material payload; use --scan for that.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
import os
from pathlib import Path
import struct
import sys
from typing import BinaryIO


HEADER3D = struct.Struct("<8s6If")
MATERIAL3D = struct.Struct("<fff")
DEFAULT_MAX_NODES3D = 36_000_000
BOUNDARIES3D = ("six-face CPML", "traction-free top, other faces CPML", "six reflecting slip walls")


@dataclass(frozen=True)
class S3dmHeader3D:
    """Describe the physical grid; CPML and ghost samples are not in the payload."""

    version3D: int
    nx3D: int
    ny3D: int
    nz3D: int
    pml3D: int
    boundary3D: int
    spacing3D: float

    @property
    def cellCount3D(self) -> int:
        """Return the physical sample count using Python's unbounded integers."""
        return self.nx3D * self.ny3D * self.nz3D

    @property
    def fileBytes3D(self) -> int:
        """Return the exact header-plus-payload byte count."""
        return HEADER3D.size + self.cellCount3D * MATERIAL3D.size


def readExact3D(stream3D: BinaryIO, count3D: int) -> bytes:
    """Read one bounded block, reporting truncated input instead of partial values."""
    block3D = stream3D.read(count3D)
    if len(block3D) != count3D:
        raise ValueError("Truncated S3DM input")
    return block3D


def readHeader3D(stream3D: BinaryIO, maxNodes3D: int = DEFAULT_MAX_NODES3D) -> S3dmHeader3D:
    """Validate magic, version, shape, spacing and exact file size before reading values."""
    if maxNodes3D < 1:
        raise ValueError("maxNodes3D must be positive")
    stream3D.seek(0)
    magic3D, *values3D = HEADER3D.unpack(readExact3D(stream3D, HEADER3D.size))
    header3D = S3dmHeader3D(*values3D)
    if magic3D != b"S3DMOD1\x00" or header3D.version3D not in (1, 2):
        raise ValueError("Unsupported S3DM magic or version")
    if min(header3D.nx3D, header3D.ny3D, header3D.nz3D) < 1:
        raise ValueError("Grid dimensions must be positive")
    if header3D.cellCount3D > maxNodes3D:
        raise ValueError(f"Physical sample count exceeds the reader limit ({maxNodes3D})")
    if header3D.boundary3D > (1 if header3D.version3D == 1 else 2):
        raise ValueError("Boundary code is invalid for this S3DM version")
    if not math.isfinite(header3D.spacing3D) or header3D.spacing3D <= 0:
        raise ValueError("Grid spacing must be finite and positive")
    if os.fstat(stream3D.fileno()).st_size != header3D.fileBytes3D:
        raise ValueError("S3DM file size does not match its grid dimensions")
    return header3D


def readModelHeader3D(path3D: str | Path, maxNodes3D: int = DEFAULT_MAX_NODES3D) -> S3dmHeader3D:
    """Inspect a model without loading or scanning its material payload."""
    with Path(path3D).open("rb") as stream3D:
        return readHeader3D(stream3D, maxNodes3D)


def validateMaterial3D(vp3D: float, vs3D: float, rho3D: float, index3D: int) -> None:
    """Reject non-finite values and non-positive isotropic bulk modulus, matching the solver's rules."""
    if not (
        all(math.isfinite(value3D) for value3D in (vp3D, vs3D, rho3D))
        and 0 < vp3D <= 30_000
        and 0 <= vs3D < vp3D
        and vp3D * vp3D > (4.0 / 3.0) * vs3D * vs3D
        and 0 < rho3D <= 50_000
    ):
        raise ValueError(f"Invalid Vp/Vs/rho at flat physical index {index3D}")


def readVoxel3D(
    path3D: str | Path, x3D: int, y3D: int, z3D: int,
    maxNodes3D: int = DEFAULT_MAX_NODES3D,
) -> tuple[float, float, float]:
    """Read and validate one zero-based XYZ sample in constant memory."""
    with Path(path3D).open("rb") as stream3D:
        header3D = readHeader3D(stream3D, maxNodes3D)
        if not (0 <= x3D < header3D.nx3D and 0 <= y3D < header3D.ny3D and 0 <= z3D < header3D.nz3D):
            raise IndexError("XYZ index is outside the physical grid")
        index3D = (z3D * header3D.ny3D + y3D) * header3D.nx3D + x3D
        stream3D.seek(HEADER3D.size + index3D * MATERIAL3D.size)
        material3D = MATERIAL3D.unpack(readExact3D(stream3D, MATERIAL3D.size))
        validateMaterial3D(*material3D, index3D)
        return material3D


def scanModel3D(
    path3D: str | Path, maxNodes3D: int = DEFAULT_MAX_NODES3D,
    chunkCells3D: int = 65_536,
) -> dict[str, list[float]]:
    """Validate all triples and compute ranges with at most 12 * chunkCells3D payload bytes per block."""
    if chunkCells3D < 1:
        raise ValueError("chunkCells3D must be positive")
    minima3D = [math.inf] * 3
    maxima3D = [-math.inf] * 3
    with Path(path3D).open("rb") as stream3D:
        header3D = readHeader3D(stream3D, maxNodes3D)
        offset3D = 0
        while offset3D < header3D.cellCount3D:
            count3D = min(chunkCells3D, header3D.cellCount3D - offset3D)
            block3D = readExact3D(stream3D, count3D * MATERIAL3D.size)
            for local3D, material3D in enumerate(MATERIAL3D.iter_unpack(block3D)):
                validateMaterial3D(*material3D, offset3D + local3D)
                for component3D, value3D in enumerate(material3D):
                    minima3D[component3D] = min(minima3D[component3D], value3D)
                    maxima3D[component3D] = max(maxima3D[component3D], value3D)
            offset3D += count3D
    return {name3D: [minima3D[index3D], maxima3D[index3D]]
            for index3D, name3D in enumerate(("vp_m_s", "vs_m_s", "rho_kg_m3"))}


def main3D() -> int:
    """Print JSON metadata, an optional voxel, or a streaming full-material scan."""
    parser3D = argparse.ArgumentParser(description=__doc__)
    parser3D.add_argument("model", type=Path, help="SeisWave Lab .s3dm model")
    parser3D.add_argument("--voxel", nargs=3, type=int, metavar=("X", "Y", "Z"), help="Read one zero-based XYZ sample")
    parser3D.add_argument("--scan", action="store_true", help="Validate every material sample and compute ranges")
    parser3D.add_argument("--max-nodes", type=int, default=DEFAULT_MAX_NODES3D, help="Reader allocation/shape guard (default: 36000000)")
    args3D = parser3D.parse_args()
    try:
        header3D = readModelHeader3D(args3D.model, args3D.max_nodes)
        result3D = asdict(header3D)
        result3D.update(fileBytes3D=header3D.fileBytes3D, cellCount3D=header3D.cellCount3D,
                        boundaryName3D=BOUNDARIES3D[header3D.boundary3D],
                        arrayOrder3D="[z][y][x][vp,vs,rho]", payloadValidated3D=False)
        if args3D.voxel is not None:
            result3D["voxelXYZ3D"] = args3D.voxel
            result3D["voxelVpVsRho3D"] = readVoxel3D(args3D.model, *args3D.voxel, maxNodes3D=args3D.max_nodes)
        if args3D.scan:
            result3D["materialRanges3D"] = scanModel3D(args3D.model, args3D.max_nodes)
            result3D["payloadValidated3D"] = True
        print(json.dumps(result3D, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, IndexError, OverflowError) as error3D:
        print(f"S3DM read failed: {error3D}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main3D())
