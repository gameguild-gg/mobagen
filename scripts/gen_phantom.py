#!/usr/bin/env python3
"""Generate a synthetic CT-like phantom as a raw R8 volume.

Stands in for a real DICOM scan while we build the load/window/spacing pipeline
(Tier 3, path B). Output is N*N*N single-byte voxels in z,y,x order — exactly the
layout the renderer uploads to a 3D texture. Swap this file for real data later.

Structure (density 0..255), a "head-ish" phantom with distinct bands so the
transfer function / isosurface / windowing actually reveal something:
  - air        (0)   outside the head
  - skull      (230) a thin outer shell
  - brain      (120) the interior
  - ventricles (40)  two low-density blobs inside
"""
import os
import struct

N = 96
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "volume.raw")


def length(x, y, z):
    return (x * x + y * y + z * z) ** 0.5


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    buf = bytearray(N * N * N)
    inv = 2.0 / (N - 1)
    for z in range(N):
        cz = z * inv - 1.0
        for y in range(N):
            cy = y * inv - 1.0
            base = (z * N + y) * N
            for x in range(N):
                cx = x * inv - 1.0
                r = length(cx, cy, cz)
                if 0.72 < r < 0.85:
                    v = 230                      # skull shell
                elif r <= 0.72:
                    v = 120                      # brain
                    d1 = length(cx + 0.18, cy, cz)
                    d2 = length(cx - 0.18, cy, cz)
                    if d1 < 0.18 or d2 < 0.18:
                        v = 40                   # ventricles
                else:
                    v = 0                        # air
                buf[base + x] = v
    with open(OUT, "wb") as f:
        f.write(buf)
    print(f"wrote {OUT}  ({N}x{N}x{N} = {len(buf)} bytes)")


if __name__ == "__main__":
    main()
