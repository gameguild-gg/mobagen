#include "volume_buffer.h"

#include <cstdio>

int main() {
  volume::VolumeMetadata meta;
  meta.width = 2;
  meta.height = 2;
  meta.depth = 1;
  meta.rescale_slope = 1.0f;
  meta.rescale_intercept = -1024.0f;
  meta.window_center = 0.0f;
  meta.window_width = 400.0f;

  const std::uint16_t stored[] = {
      824,   // -200 HU -> black edge of this window
      1024,  //    0 HU -> middle gray
      1224,  //  200 HU -> white edge
      1424,  //  400 HU -> clamped white
  };

  volume::VolumeBuffer heap = volume::VolumeBuffer::from_u16_windowed(meta, stored);
  if (heap.size_bytes() != 4 || heap.data()[0] != 0 || heap.data()[3] != 255) {
    std::printf("heap VolumeBuffer conversion failed\n");
    return 1;
  }

  volume::VolumeArena arena(1024);
  volume::VolumeBuffer arena_backed = volume::VolumeBuffer::from_u16_windowed(meta, stored, arena.resource());
  if (arena_backed.size_bytes() != 4 || arena_backed.data()[1] < 120 || arena_backed.data()[1] > 136) {
    std::printf("arena VolumeBuffer conversion failed\n");
    return 1;
  }

  volume::VolumeBuffer packed = volume::VolumeBuffer::from_u16_packed_rg8(meta, stored);
  if (packed.storage_format() != volume::VolumeStorageFormat::U16PackedRG8 || packed.bytes_per_voxel() != 2 || packed.size_bytes() != 8
      || packed.data()[0] != static_cast<std::uint8_t>(824 & 0x00ffu) || packed.data()[1] != static_cast<std::uint8_t>(824 >> 8u)) {
    std::printf("packed UInt16 VolumeBuffer conversion failed\n");
    return 1;
  }

  std::printf("VolumeBuffer OK: R8 windowed + packed UInt16 ownership paths\n");
  return 0;
}
