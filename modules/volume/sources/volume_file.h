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
#include <memory>
#include <new>

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
  // and ok=false on any problem (missing file, invalid metadata, or size mismatch).
  inline VolumeBuffer load_volume_file(const char* path, bool& ok) {
    ok = false;
    if (path == nullptr || path[0] == '\0') return {};
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"),
                                                           &std::fclose);
    if (!file) return {};

    VolumeFileHeader h{};
    if (std::fread(&h, sizeof(h), 1, file.get()) != 1
        || std::memcmp(h.magic, "MVL1", 4) != 0)
      return {};

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
    VolumeStorageFormat format;
    if (h.storage_format == 0 && h.bytes_per_voxel == 1) {
      format = VolumeStorageFormat::R8;
    } else if (h.storage_format == 1 && h.bytes_per_voxel == 2) {
      format = VolumeStorageFormat::U16PackedRG8;
    } else {
      return {};
    }

    VolumeLayout layout;
    if (!try_volume_layout(meta, format, h.bytes_per_voxel, layout)) return {};
    if (std::fseek(file.get(), 0, SEEK_END) != 0) return {};
    const long file_size = std::ftell(file.get());
    const std::size_t expected_size = sizeof(VolumeFileHeader) + layout.byte_count;
    if (file_size < 0 || static_cast<std::uint64_t>(file_size) != expected_size
        || std::fseek(file.get(), static_cast<long>(sizeof(VolumeFileHeader)), SEEK_SET) != 0)
      return {};

    try {
      VolumeBuffer buffer(meta, format, h.bytes_per_voxel, std::pmr::get_default_resource());
      if (buffer.size_bytes() != layout.byte_count) return {};
      if (std::fread(buffer.data(), 1, buffer.size_bytes(), file.get()) != buffer.size_bytes()) {
        return {};
      }
      ok = true;
      return buffer;
    } catch (const std::bad_alloc&) {
      return {};
    }
  }

}  // namespace volume
