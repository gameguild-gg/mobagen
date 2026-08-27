// Standalone smoke test for the volume_io DICOM loader. Built only when
// -DUSE_GDCM=ON (native). Loads a series and prints what it found — a fast,
// renderer-independent check before wiring the loader into the engine.
//
//   volume_io_test [dir]      (default dir: apps/dicom_viewer/assets/dicom)

#include "volume_io.h"

#include <cstdio>

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : "apps/dicom_viewer/assets/dicom";

  VolumeData v = volume_io_load_series(dir);
  if (!v.voxels) {
    std::printf("FAILED to load DICOM series from '%s'\n", dir);
    return 1;
  }

  std::printf("loaded %d x %d x %d voxels\n", v.width, v.height, v.depth);
  std::printf("spacing  %.3f x %.3f x %.3f mm\n", v.spacing_x, v.spacing_y, v.spacing_z);
  std::printf("rescale  slope %.3f  intercept %.1f\n", v.rescale_slope, v.rescale_intercept);
  std::printf("window   center %.1f  width %.1f (HU)\n", v.window_center, v.window_width);
  std::printf("stored   range [%.0f, %.0f]\n", v.value_min, v.value_max);

  // Center voxel — should land in the phantom's brain (~40 HU).
  const std::size_t c = (static_cast<std::size_t>(v.depth / 2) * v.height + v.height / 2) * v.width + v.width / 2;
  const double hu = v.voxels[c] * v.rescale_slope + v.rescale_intercept;
  std::printf("center   stored %u  ->  %.1f HU\n", static_cast<unsigned>(v.voxels[c]), hu);

  volume_io_free(&v);
  return 0;
}
