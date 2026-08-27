#pragma once
// ============================================================================
// .mvol — a compact on-disk volume for the WEB build (and as a native cache).
// ============================================================================
// GDCM only runs in the native build; the browser cannot parse DICOM. So the
// DICOM series is converted offline (scripts/dicom_to_mvol.py, pure Python) into
// this format: a fixed 64-byte little-endian header + the raw voxel payload.
// The wasm build preloads it and reconstructs the exact same volume::VolumeBuffer
// the native GDCM path produces — so real DICOM stored values (GPU window/level
// + the histogram auto-window) work in the browser, where GDCM is unavailable.
//
// The Python writer and this reader MUST keep the header layout in lock-step.

#include "volume_buffer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace volume {

#pragma pack(push, 1)
  struct VolumeFileHeader {
    char magic[4];  // "MVL1"
    std::uint32_t width, height, depth;
    std::uint32_t storage_format;   // 0 = R8, 1 = U16PackedRG8 (VolumeStorageFormat)
    std::uint32_t bytes_per_voxel;  // 1 or 2
    float spacing_x, spacing_y, spacing_z;
    float rescale_slope, rescale_intercept;
    float window_center, window_width;  // clinical (HU) units
    float value_min, value_max;         // stored-value range
    std::uint32_t reserved;             // pads the header to 64 bytes
  };
#pragma pack(pop)
  static_assert(sizeof(VolumeFileHeader) == 64, "VolumeFileHeader must be 64 bytes");

  // Load a .mvol into a VolumeBuffer (with full metadata). Returns an empty buffer
  // and ok=false on any problem (missing file, bad magic, short read).
  inline VolumeBuffer load_volume_file(const char* path, bool& ok) {
    ok = false;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};

    VolumeFileHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1 || std::memcmp(h.magic, "MVL1", 4) != 0) {
      std::fclose(f);
      return {};
    }

    VolumeMetadata meta;
    meta.width = h.width;
    meta.height = h.height;
    meta.depth = h.depth;
    meta.spacing_mm = {h.spacing_x, h.spacing_y, h.spacing_z};
    meta.rescale_slope = h.rescale_slope;
    meta.rescale_intercept = h.rescale_intercept;
    meta.window_center = h.window_center;
    meta.window_width = h.window_width;
    meta.value_min = h.value_min;
    meta.value_max = h.value_max;
    if (!meta.valid()) {
      std::fclose(f);
      return {};
    }

    const VolumeStorageFormat fmt = (h.storage_format == 1) ? VolumeStorageFormat::U16PackedRG8 : VolumeStorageFormat::R8;
    VolumeBuffer buffer(meta, fmt, h.bytes_per_voxel ? h.bytes_per_voxel : 1u, std::pmr::get_default_resource());
    if (!buffer.empty()) {
      const std::size_t got = std::fread(buffer.data(), 1, buffer.size_bytes(), f);
      if (got != buffer.size_bytes()) {
        std::fclose(f);
        return {};
      }
    }
    std::fclose(f);
    ok = !buffer.empty();
    return buffer;
  }

}  // namespace volume
