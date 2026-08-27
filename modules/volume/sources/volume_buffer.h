#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <vector>

#include <glm/glm.hpp>

namespace volume {

  enum class VolumeStorageFormat : std::uint8_t { R8, U16PackedRG8 };

  struct VolumeMetadata {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    glm::vec3 spacing_mm{1.0f};

    // Clinical intensity metadata. DICOM usually stores integer samples and
    // recovers Hounsfield units as stored*slope + intercept.
    float rescale_slope = 1.0f;
    float rescale_intercept = 0.0f;
    float window_center = 0.5f;
    float window_width = 1.0f;
    float value_min = 0.0f;
    float value_max = 1.0f;

    std::size_t voxel_count() const { return static_cast<std::size_t>(width) * height * depth; }

    bool valid() const { return width > 0 && height > 0 && depth > 0; }
  };

  // A deliberately small monotonic arena for voxel bytes. This is not a general
  // GC; it is a study tool for large volumes where you want one bulk allocation
  // and one bulk reset instead of many heap allocations.
  class VolumeArena {
  public:
    explicit VolumeArena(std::size_t capacity_bytes) : storage_(capacity_bytes), resource_(storage_.data(), storage_.size()) {}

    std::pmr::memory_resource* resource() { return &resource_; }

    void reset() { resource_.release(); }

    std::size_t capacity_bytes() const { return storage_.size(); }

  private:
    std::vector<std::byte> storage_;
    std::pmr::monotonic_buffer_resource resource_;
  };

  // CPU-side volume bytes ready for GPU upload. The important ownership point:
  // this object owns the bytes. The renderer may upload them to a GPU texture, but
  // it does not own or free DICOM loader memory.
  class VolumeBuffer {
  public:
    using Storage = std::pmr::vector<std::uint8_t>;

    VolumeBuffer() = default;

    VolumeBuffer(VolumeMetadata metadata, std::pmr::memory_resource* resource) : VolumeBuffer(metadata, VolumeStorageFormat::R8, 1, resource) {}

    VolumeBuffer(VolumeMetadata metadata, VolumeStorageFormat format, std::uint32_t bytes_per_voxel, std::pmr::memory_resource* resource)
        : metadata_(metadata), format_(format), bytes_per_voxel_(std::max(bytes_per_voxel, 1u)), bytes_(resource) {
      bytes_.resize(metadata.voxel_count() * bytes_per_voxel_);
    }

    static VolumeBuffer from_u8(VolumeMetadata metadata, const std::uint8_t* src,
                                std::pmr::memory_resource* resource = std::pmr::get_default_resource()) {
      VolumeBuffer out(metadata, resource);
      if (src && !out.bytes_.empty()) {
        std::memcpy(out.bytes_.data(), src, out.bytes_.size());
      }
      return out;
    }

    // Preserve DICOM's 16-bit stored values for GPU-side windowing. WebGPU can
    // sample RG8 textures everywhere, so we pack one little-endian UInt16 voxel
    // into two normalized 8-bit channels:
    //
    //   R = low byte, G = high byte
    //
    // WGSL reconstructs: stored = round(R*255) + round(G*255)*256
    static VolumeBuffer from_u16_packed_rg8(VolumeMetadata metadata, const std::uint16_t* src,
                                            std::pmr::memory_resource* resource = std::pmr::get_default_resource()) {
      VolumeBuffer out(metadata, VolumeStorageFormat::U16PackedRG8, 2, resource);
      if (!src || out.bytes_.empty()) return out;

      for (std::size_t i = 0; i < metadata.voxel_count(); ++i) {
        const std::uint16_t value = src[i];
        out.bytes_[i * 2 + 0] = static_cast<std::uint8_t>(value & 0x00ffu);
        out.bytes_[i * 2 + 1] = static_cast<std::uint8_t>((value >> 8u) & 0x00ffu);
      }
      return out;
    }

    static VolumeBuffer from_u16_windowed(VolumeMetadata metadata, const std::uint16_t* src,
                                          std::pmr::memory_resource* resource = std::pmr::get_default_resource()) {
      VolumeBuffer out(metadata, resource);
      if (!src || out.bytes_.empty()) return out;

      const float width = std::max(metadata.window_width, 1.0f);
      const float lo = metadata.window_center - width * 0.5f;

      for (std::size_t i = 0; i < out.bytes_.size(); ++i) {
        const float hu = static_cast<float>(src[i]) * metadata.rescale_slope + metadata.rescale_intercept;
        const float normalized = std::clamp((hu - lo) / width, 0.0f, 1.0f);
        out.bytes_[i] = static_cast<std::uint8_t>(normalized * 255.0f + 0.5f);
      }
      return out;
    }

    const VolumeMetadata& metadata() const { return metadata_; }
    VolumeStorageFormat storage_format() const { return format_; }
    std::uint32_t bytes_per_voxel() const { return bytes_per_voxel_; }
    const std::uint8_t* data() const { return bytes_.data(); }
    std::uint8_t* data() { return bytes_.data(); }
    std::size_t size_bytes() const { return bytes_.size(); }
    bool empty() const { return bytes_.empty(); }

  private:
    VolumeMetadata metadata_{};
    VolumeStorageFormat format_ = VolumeStorageFormat::R8;
    std::uint32_t bytes_per_voxel_ = 1;
    Storage bytes_{std::pmr::get_default_resource()};
  };

}  // namespace volume
