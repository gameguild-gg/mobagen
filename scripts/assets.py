#!/usr/bin/env python3
"""Small asset manager for reproducible local volume data.

Why this exists:
  - Git should store code, manifests, and reproducible recipes.
  - Large/binary datasets should be downloaded or generated locally.
    - The DICOM viewer wants stable paths under apps/dicom_viewer/assets, so this
        tool materializes those paths on demand.

Commands:
    python scripts/assets.py list
    python scripts/assets.py ensure
    python scripts/assets.py verify
    python scripts/assets.py clean
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "apps" / "dicom_viewer" / "assets" / "assets.json"
MVOL_HEADER = "<4s3I2I9fI"


def rel(path: str | Path) -> Path:
    return ROOT / path


def load_manifest() -> dict:
    with MANIFEST_PATH.open("r", encoding="utf-8") as f:
        return json.load(f)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require_sha(path: Path, expected: str, label: str) -> None:
    got = sha256_file(path)
    if got != expected:
        raise SystemExit(f"{label} hash mismatch:\n  file: {path}\n  got : {got}\n  want: {expected}")


def download(url: str, out: Path, expected_sha: str) -> None:
    if out.exists() and sha256_file(out) == expected_sha:
        print(f"ok   {out.relative_to(ROOT)}")
        return

    out.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=out.name + ".", suffix=".part", dir=str(out.parent))
    os.close(fd)
    tmp = Path(tmp_name)
    try:
        print(f"get  {url}")
        with urllib.request.urlopen(url) as response, tmp.open("wb") as f:
            shutil.copyfileobj(response, f)
        require_sha(tmp, expected_sha, "download")
        tmp.replace(out)
        print(f"wrote {out.relative_to(ROOT)}")
    finally:
        if tmp.exists():
            tmp.unlink()


def extract_raw(source: dict) -> None:
    archive = rel(source["cache"])
    raw = rel(source["raw"])
    if raw.exists() and sha256_file(raw) == source["raw_sha256"]:
        print(f"ok   {raw.relative_to(ROOT)}")
        return

    raw.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        data = z.read(source["member"])
    raw.write_bytes(data)
    require_sha(raw, source["raw_sha256"], "raw extraction")
    print(f"wrote {raw.relative_to(ROOT)}")


def generate_phantom(output: dict) -> None:
    out = rel(output["path"])
    expected = output.get("sha256")
    if expected and out.exists() and sha256_file(out) == expected:
        print(f"ok   {out.relative_to(ROOT)}")
        return

    n = int(output["dimensions"][0])
    if output["dimensions"] != [n, n, n]:
        raise SystemExit("phantom-r8 expects cubic dimensions")

    out.parent.mkdir(parents=True, exist_ok=True)
    buf = bytearray(n * n * n)
    inv = 2.0 / (n - 1)

    def length(x: float, y: float, z: float) -> float:
        return math.sqrt(x * x + y * y + z * z)

    for z in range(n):
        cz = z * inv - 1.0
        for y in range(n):
            cy = y * inv - 1.0
            base = (z * n + y) * n
            for x in range(n):
                cx = x * inv - 1.0
                r = length(cx, cy, cz)
                if 0.72 < r < 0.85:
                    v = 230
                elif r <= 0.72:
                    v = 120
                    if length(cx + 0.18, cy, cz) < 0.18 or length(cx - 0.18, cy, cz) < 0.18:
                        v = 40
                else:
                    v = 0
                buf[base + x] = v

    out.write_bytes(buf)
    if expected:
        require_sha(out, expected, "phantom")
    print(f"wrote {out.relative_to(ROOT)} ({n}x{n}x{n} R8)")


def nearest_indices(src_count: int, dst_count: int) -> list[int]:
    if dst_count <= 1:
        return [0]
    step = (src_count - 1) / (dst_count - 1)
    return [int(round(i * step)) for i in range(dst_count)]


def generate_mvol(output: dict, sources: dict) -> None:
    out = rel(output["path"])
    expected = output.get("sha256")
    if expected and out.exists() and sha256_file(out) == expected:
        print(f"ok   {out.relative_to(ROOT)}")
        return

    source = sources[output["source"]]
    if source["dtype"] != "uint8":
        raise SystemExit("mvol-from-raw currently supports uint8 source data")

    raw = rel(source["raw"])
    width, height, depth = source["dimensions"]
    data = raw.read_bytes()
    if len(data) != width * height * depth:
        raise SystemExit(f"raw size mismatch for {raw}: {len(data)} bytes")

    max_dim = int(output["max_dimension"])
    scale = max_dim / max(width, height, depth)
    target_w = max(1, round(width * scale))
    target_h = max(1, round(height * scale))
    target_d = max(1, round(depth * scale))

    xs = nearest_indices(width, target_w)
    ys = nearest_indices(height, target_h)
    zs = nearest_indices(depth, target_d)
    flip = output.get("flip", "")
    if "x" in flip:
        xs.reverse()
    if "y" in flip:
        ys.reverse()
    if "z" in flip:
        zs.reverse()

    payload = bytearray(target_w * target_h * target_d)
    o = 0
    for z in zs:
        zbase = z * width * height
        for y in ys:
            row = zbase + y * width
            for x in xs:
                payload[o] = data[row + x]
                o += 1

    spacing = output.get("spacing_mm", [1.0, 1.0, 1.0])
    header = struct.pack(
        MVOL_HEADER,
        b"MVL1",
        target_w,
        target_h,
        target_d,
        0,  # storage format: R8
        1,  # bytes per voxel
        float(spacing[0]),
        float(spacing[1]),
        float(spacing[2]),
        1.0,   # slope
        0.0,   # intercept
        127.5, # window center
        255.0, # window width
        0.0,
        255.0,
        0,
    )

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + payload)
    if expected:
        require_sha(out, expected, "mvol")
    print(f"wrote {out.relative_to(ROOT)} ({target_w}x{target_h}x{target_d} R8)")


def ensure(manifest: dict) -> None:
    for source in manifest["sources"].values():
        if source["kind"] != "zip-raw-volume":
            raise SystemExit(f"unknown source kind: {source['kind']}")
        download(source["url"], rel(source["cache"]), source["sha256"])
        extract_raw(source)

    for output in manifest["outputs"]:
        kind = output["kind"]
        if kind == "phantom-r8":
            generate_phantom(output)
        elif kind == "mvol-from-raw":
            generate_mvol(output, manifest["sources"])
        else:
            raise SystemExit(f"unknown output kind: {kind}")


def verify(manifest: dict) -> None:
    for source in manifest["sources"].values():
        require_sha(rel(source["cache"]), source["sha256"], source["cache"])
        require_sha(rel(source["raw"]), source["raw_sha256"], source["raw"])
        print(f"ok   {source['cache']}")
        print(f"ok   {source['raw']}")
    for output in manifest["outputs"]:
        require_sha(rel(output["path"]), output["sha256"], output["path"])
        print(f"ok   {output['path']}")


def clean(manifest: dict) -> None:
    paths: set[Path] = set()
    for source in manifest["sources"].values():
        paths.add(rel(source["cache"]))
        paths.add(rel(source["raw"]))
    for output in manifest["outputs"]:
        paths.add(rel(output["path"]))
    for path in sorted(paths):
        if path.exists():
            path.unlink()
            print(f"removed {path.relative_to(ROOT)}")


def list_assets(manifest: dict) -> None:
    print("sources:")
    for name, source in manifest["sources"].items():
        print(f"  {name}: {source['dimensions']} {source['dtype']} -> {source['raw']}")
    print("outputs:")
    for output in manifest["outputs"]:
        print(f"  {output['id']}: {output['kind']} -> {output['path']}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["list", "ensure", "verify", "clean"])
    args = parser.parse_args()

    manifest = load_manifest()
    if args.command == "list":
        list_assets(manifest)
    elif args.command == "ensure":
        ensure(manifest)
    elif args.command == "verify":
        verify(manifest)
    elif args.command == "clean":
        clean(manifest)


if __name__ == "__main__":
    main()
