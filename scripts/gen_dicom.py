#!/usr/bin/env python3
"""Generate a synthetic CT series as real DICOM files (for testing the loader).

We don't have a real scan handy and downloading one is blocked here, so this
emits a valid DICOM series with the tags a real loader must handle:
  - 16-bit pixel data, RescaleSlope/Intercept (Hounsfield units),
  - PixelSpacing + SliceThickness (anisotropic: 1.0 x 1.0 in-plane, 1.5 between
    slices — matches the renderer's box scaling),
  - WindowCenter/Width, per-slice ImagePositionPatient, shared Series UID.

The phantom is the same "head" (air / skull / brain / ventricles) but in real
Hounsfield values, so window/level and the transfer function behave like a scan.
Swap this for a real CT series later — the loader won't care.
"""
import os
import datetime
import numpy as np
import pydicom
from pydicom.dataset import FileDataset, FileMetaDataset
from pydicom.uid import generate_uid, ExplicitVRLittleEndian

CT_IMAGE_STORAGE = "1.2.840.10008.5.1.4.1.1.2"
N = 96
SPACING_XY = 1.0
SPACING_Z = 1.5
INTERCEPT = -1024            # stored = HU - intercept = HU + 1024 (unsigned 16-bit)
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "dicom")

# Hounsfield values for each tissue.
HU_AIR, HU_BRAIN, HU_VENTRICLE, HU_SKULL = -1000, 40, 10, 1000


def hu_volume(n):
    v = np.full((n, n, n), HU_AIR, dtype=np.int16)
    inv = 2.0 / (n - 1)
    for z in range(n):
        cz = z * inv - 1.0
        for y in range(n):
            cy = y * inv - 1.0
            for x in range(n):
                cx = x * inv - 1.0
                r = (cx * cx + cy * cy + cz * cz) ** 0.5
                if 0.72 < r < 0.85:
                    v[z, y, x] = HU_SKULL
                elif r <= 0.72:
                    d1 = ((cx + 0.18) ** 2 + cy * cy + cz * cz) ** 0.5
                    d2 = ((cx - 0.18) ** 2 + cy * cy + cz * cz) ** 0.5
                    v[z, y, x] = HU_VENTRICLE if (d1 < 0.18 or d2 < 0.18) else HU_BRAIN
    return v


def main():
    os.makedirs(OUT, exist_ok=True)
    vol = hu_volume(N)
    study_uid = generate_uid()
    series_uid = generate_uid()
    frame_uid = generate_uid()
    now = datetime.datetime.now()

    for z in range(N):
        meta = FileMetaDataset()
        meta.MediaStorageSOPClassUID = CT_IMAGE_STORAGE
        meta.MediaStorageSOPInstanceUID = generate_uid()
        meta.TransferSyntaxUID = ExplicitVRLittleEndian

        ds = FileDataset(None, {}, file_meta=meta, preamble=b"\0" * 128)
        ds.Modality = "CT"
        ds.SOPClassUID = CT_IMAGE_STORAGE
        ds.SOPInstanceUID = meta.MediaStorageSOPInstanceUID
        ds.StudyInstanceUID = study_uid
        ds.SeriesInstanceUID = series_uid
        ds.FrameOfReferenceUID = frame_uid
        ds.PatientName = "PHANTOM^HEAD"
        ds.PatientID = "PHANTOM001"
        ds.StudyDate = now.strftime("%Y%m%d")
        ds.SeriesNumber = 1
        ds.InstanceNumber = z + 1

        ds.Rows = N
        ds.Columns = N
        ds.PixelSpacing = [SPACING_XY, SPACING_XY]
        ds.SliceThickness = SPACING_Z
        ds.SpacingBetweenSlices = SPACING_Z
        ds.ImagePositionPatient = [0.0, 0.0, z * SPACING_Z]
        ds.ImageOrientationPatient = [1, 0, 0, 0, 1, 0]

        ds.SamplesPerPixel = 1
        ds.PhotometricInterpretation = "MONOCHROME2"
        ds.BitsAllocated = 16
        ds.BitsStored = 16
        ds.HighBit = 15
        ds.PixelRepresentation = 0           # unsigned
        ds.RescaleIntercept = INTERCEPT
        ds.RescaleSlope = 1
        ds.WindowCenter = 40                 # brain window
        ds.WindowWidth = 400

        stored = (vol[z].astype(np.int32) - INTERCEPT).astype(np.uint16)
        ds.PixelData = stored.tobytes()

        ds.is_little_endian = True
        ds.is_implicit_VR = False
        ds.save_as(os.path.join(OUT, f"slice_{z:03d}.dcm"), write_like_original=False)

    print(f"wrote {N} DICOM slices to {OUT}  ({N}x{N}x{N}, spacing {SPACING_XY}x{SPACING_XY}x{SPACING_Z})")


if __name__ == "__main__":
    main()
