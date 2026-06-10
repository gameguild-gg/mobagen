#!/usr/bin/env python3
"""Convert a DICOM series to a compact .mvol volume for the WEB build.

GDCM runs only in the native build; the browser build cannot parse DICOM. This
tool (Python + pydicom, run offline) reads a series, stacks the 16-bit stored
values, packs them into RG8 (low byte, high byte) and writes a 64-byte header +
payload. The wasm build preloads + reconstructs the same volume::VolumeBuffer the
native GDCM path makes, so real DICOM intensities (GPU window/level + histogram
auto-window) work in the browser.

Header layout MUST stay in lock-step with modules/volume/sources/volume_file.h:
  <4s 3I 2I 9f I>  =  magic, w,h,d, storage_format,bytes_per_voxel,
                      spacing_xyz, slope, intercept, win_center, win_width,
                      value_min, value_max, reserved   (64 bytes)
Packing matches volume::VolumeBuffer::from_u16_packed_rg8 (R=low, G=high).

Usage: python scripts/dicom_to_mvol.py [dicom_dir] [out.mvol]
"""
import glob
import os
import struct
import sys

import numpy as np
import pydicom

HERE = os.path.dirname(os.path.abspath(__file__))
DICOM_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "apps", "dicom_viewer", "assets", "dicom")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "apps", "dicom_viewer", "assets", "volume.mvol")

MAGIC = b"MVL1"
STORAGE_U16_PACKED_RG8 = 1
HEADER_FMT = "<4s3I2I9fI"  # 64 bytes


def _scalar(v, default):
    """DICOM WindowCenter/Width can be a single value or a MultiValue."""
    if v is None:
        return float(default)
    try:
        return float(v[0])
    except (TypeError, IndexError):
        return float(v)


def main():
    files = sorted(glob.glob(os.path.join(DICOM_DIR, "*.dcm")))
    if not files:
        sys.exit(f"no .dcm files found in {DICOM_DIR}")

    slices = [pydicom.dcmread(f) for f in files]

    # Sort by physical slice position (z); fall back to InstanceNumber. This must
    # match the order the native GDCM loader stacks slices in.
    def zkey(ds):
        try:
            return float(ds.ImagePositionPatient[2])
        except Exception:
            return float(getattr(ds, "InstanceNumber", 0))

    slices.sort(key=zkey)

    s0 = slices[0]
    width = int(s0.Columns)
    height = int(s0.Rows)
    depth = len(slices)

    # Stack stored (pre-rescale) 16-bit values: shape (depth, height, width),
    # C-order flatten => x fastest, then y, then z (index = x + y*w + z*w*h).
    vol = np.zeros((depth, height, width), dtype=np.uint16)
    for i, ds in enumerate(slices):
        vol[i] = ds.pixel_array.astype(np.uint16)

    ps = [float(x) for x in getattr(s0, "PixelSpacing", [1.0, 1.0])]  # [row(y), col(x)]
    spacing_y, spacing_x = ps[0], ps[1]
    spacing_z = float(getattr(s0, "SpacingBetweenSlices",
                              getattr(s0, "SliceThickness", 1.0)))
    slope = float(getattr(s0, "RescaleSlope", 1.0))
    intercept = float(getattr(s0, "RescaleIntercept", 0.0))
    win_center = _scalar(getattr(s0, "WindowCenter", None), 0.5)
    win_width = _scalar(getattr(s0, "WindowWidth", None), 1.0)
    value_min = float(vol.min())
    value_max = float(vol.max())

    # Pack uint16 -> interleaved RG8 (low byte in R, high byte in G).
    flat = vol.reshape(-1)
    rg = np.empty(flat.size * 2, dtype=np.uint8)
    rg[0::2] = (flat & 0xFF).astype(np.uint8)
    rg[1::2] = ((flat >> 8) & 0xFF).astype(np.uint8)

    header = struct.pack(
        HEADER_FMT, MAGIC, width, height, depth,
        STORAGE_U16_PACKED_RG8, 2,
        spacing_x, spacing_y, spacing_z, slope, intercept,
        win_center, win_width, value_min, value_max, 0)
    assert len(header) == 64, len(header)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(header)
        f.write(rg.tobytes())

    print(f"wrote {OUT}")
    print(f"  {width}x{height}x{depth} packed RG8, payload {rg.size} bytes")
    print(f"  spacing {spacing_x}x{spacing_y}x{spacing_z} mm, "
          f"slope {slope}, intercept {intercept}, window {win_center}/{win_width} HU")
    print(f"  stored value range [{value_min}, {value_max}]")


if __name__ == "__main__":
    main()
