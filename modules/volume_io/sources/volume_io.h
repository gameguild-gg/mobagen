#pragma once

// ============================================================================
// volume_io — a SEPARATE module that loads medical volume data.
// ============================================================================
//
// This is decoupled from the graphics engine on purpose: the renderer never
// sees GDCM/DICOM, only this flat C struct. The implementation (volume_io.cpp)
// uses GDCM; it could later be swapped for ITK or anything else without touching
// the renderer. A C ABI (not C++) keeps the boundary stable across compilers and
// callable from other languages — the same idea as engine_c.h.
//
// Flow:  volume_io_load_series(dir) -> VolumeData -> engine uploads to Texture3D,
//        sets box half-extents from spacing, window from window_center/width.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A loaded volume plus the metadata the renderer needs. Owns `voxels` until
// volume_io_free() is called. On failure, `voxels` is NULL and dims are 0.
typedef struct VolumeData {
  uint16_t* voxels;  // width*height*depth stored samples (row-major, z outer)
  int width;
  int height;
  int depth;

  float spacing_x;  // mm per voxel along each axis
  float spacing_y;
  float spacing_z;

  float rescale_slope;  // Hounsfield: HU = stored * slope + intercept
  float rescale_intercept;

  float window_center;  // default display window (in HU)
  float window_width;

  float value_min;  // observed stored-value range (for normalization)
  float value_max;
} VolumeData;

// Load a DICOM series from a directory of .dcm files. `dir` is a path in the
// WASM virtual filesystem (web) or a real path (native). Slices are sorted by
// position and stacked. Returns voxels==NULL on any failure.
VolumeData volume_io_load_series(const char* dir);

// Free the voxel buffer from a successful load (and zero the struct).
void volume_io_free(VolumeData* v);

#ifdef __cplusplus
}  // extern "C"
#endif
