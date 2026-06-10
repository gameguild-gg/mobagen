#!/usr/bin/env python3
"""Save max-intensity-projection PNGs of a raw volume, to inspect orientation.
Usage: python scripts/mip_preview.py <in.raw> <W> <H> <D> <uint8|uint16> [outdir]
"""
import os
import sys

import numpy as np
from PIL import Image

inp = sys.argv[1]
W, H, D = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
dt = np.uint8 if sys.argv[5] == "uint8" else np.uint16
outdir = sys.argv[6] if len(sys.argv) > 6 else "build"

vol = np.fromfile(inp, dtype=dt).reshape(D, H, W)  # (z, y, x)
views = {
    "axial_z":    vol.max(axis=0),   # (H, W) — looking down the slice axis
    "coronal_y":  vol.max(axis=1),   # (D, W) — row 0 = slice 0
    "sagittal_x": vol.max(axis=2),   # (D, H) — row 0 = slice 0
}
os.makedirs(outdir, exist_ok=True)
for name, img in views.items():
    a = img.astype(np.float64)
    a = (a / max(a.max(), 1.0) * 255.0).astype(np.uint8)
    Image.fromarray(a).save(os.path.join(outdir, f"mip_{name}.png"))
    print(f"{name}: shape {img.shape} -> build/mip_{name}.png")
