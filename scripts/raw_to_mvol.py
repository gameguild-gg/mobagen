#!/usr/bin/env python3
"""Convert a raw scalar volume (a REAL CT, e.g. open-scivis-datasets) to .mvol
for the web build, downsampling to a web-friendly size.

Real CT volumes are large (256^3 uint8 = 16 MB; clinical 512x512xN x 2 bytes =
tens of MB) -- too big to preload into the wasm FS. This downsamples to a target
max dimension and writes the SAME .mvol format the DICOM path uses (see
modules/volume/sources/volume_file.h), so the browser renders REAL anatomy instead of the
synthetic phantom. uint8 source -> R8; uint16 source -> packed RG8.

Usage:
    python scripts/raw_to_mvol.py <in.raw> <W> <H> <D> <uint8|uint16> [out.mvol] [max_dim]
"""
import os
import struct
import sys

import numpy as np

HEADER_FMT = "<4s3I2I9fI"  # 64 bytes; matches volume_file.h / dicom_to_mvol.py
MAGIC = b"MVL1"


def downsample(vol, td, th, tw):
    """vol is (D,H,W). Nearest-neighbour pick to target dims (robust, dep-free)."""
    d, h, w = vol.shape
    zi = np.linspace(0, d - 1, td).round().astype(np.int64)
    yi = np.linspace(0, h - 1, th).round().astype(np.int64)
    xi = np.linspace(0, w - 1, tw).round().astype(np.int64)
    return vol[np.ix_(zi, yi, xi)]


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    inp = sys.argv[1]
    W, H, D = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    dtype = sys.argv[5]
    out = sys.argv[6] if len(sys.argv) > 6 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "apps", "dicom_viewer", "assets", "volume.mvol")
    max_dim = int(sys.argv[7]) if len(sys.argv) > 7 else 128

    np_dtype = np.uint8 if dtype == "uint8" else np.uint16
    raw = np.fromfile(inp, dtype=np_dtype)
    if raw.size != W * H * D:
        sys.exit(f"size mismatch: {raw.size} voxels != {W*H*D} (W*H*D)")
    vol = raw.reshape((D, H, W))

    # Scale so the largest dimension == max_dim, keep aspect ratio.
    scale = max_dim / max(W, H, D)
    tW = max(1, round(W * scale))
    tH = max(1, round(H * scale))
    tD = max(1, round(D * scale))
    vol = downsample(vol, tD, tH, tW)

    # Optional axis flips to fix orientation (e.g. "y" turns an upside-down head
    # right-side up; combine like "yz"). z=slices, y=rows, x=cols.
    flip = sys.argv[8] if len(sys.argv) > 8 else ""
    if "z" in flip: vol = vol[::-1, :, :]
    if "y" in flip: vol = vol[:, ::-1, :]
    if "x" in flip: vol = vol[:, :, ::-1]

    depth, height, width = vol.shape

    if dtype == "uint8":
        storage_fmt, bpv = 0, 1  # R8
        payload = np.ascontiguousarray(vol, dtype=np.uint8).reshape(-1).tobytes()
        vmin, vmax = 0.0, 255.0
    else:
        storage_fmt, bpv = 1, 2  # packed RG8 (low, high)
        flat = np.ascontiguousarray(vol, dtype=np.uint16).reshape(-1)
        rg = np.empty(flat.size * 2, dtype=np.uint8)
        rg[0::2] = (flat & 0xFF).astype(np.uint8)
        rg[1::2] = ((flat >> 8) & 0xFF).astype(np.uint8)
        payload = rg.tobytes()
        vmin, vmax = float(flat.min()), float(flat.max())

    # Bare raw has no spacing/clinical tags -> 1 mm isotropic, mid-range window.
    header = struct.pack(
        HEADER_FMT, MAGIC, width, height, depth, storage_fmt, bpv,
        1.0, 1.0, 1.0, 1.0, 0.0, (vmin + vmax) * 0.5, (vmax - vmin), vmin, vmax, 0)
    assert len(header) == 64, len(header)

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(header)
        f.write(payload)
    print(f"wrote {out}")
    print(f"  {width}x{height}x{depth} {'R8' if bpv == 1 else 'packed RG8'}, "
          f"{len(payload)} bytes payload (downsampled from {W}x{H}x{D} {dtype})")


if __name__ == "__main__":
    main()
