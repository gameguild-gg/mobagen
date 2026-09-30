#ifndef MOBAGEN_MEMORY_OBJECT_HEADER_H
#define MOBAGEN_MEMORY_OBJECT_HEADER_H

/*
 * Mobagen shared-region object format v1 (todo 12).
 *
 * C11 on purpose: the SAME header layout is interpreted by the host (this
 * memory core) and by module guest code compiled against the sdk. Every
 * address inside the shared region is expressed as a 32-bit OFFSET from the
 * region base so the format is identical on native (WAMR shared heap) and on
 * wasm32 (SharedArrayBuffer); a region therefore never exceeds 4 GiB.
 *
 * Region layout (managed by mobagen::memory::MemoryManager):
 *
 *   [ control block | handle table | object chunks ... ]
 *
 * The object area is carved into chunks; every chunk serves exactly one size
 * class and is split into fixed-size blocks. A block is either free (on the
 * per-class free list, threaded through the first payload word) or allocated
 * to exactly one object:
 *
 *   +0  u32 magic_and_version   == MOBAGEN_MEMORY_OBJECT_MAGIC_V1
 *   +4  u32 flags               [7:0] size class, bit8 mark, bit9 free
 *   +8  u32 payload_bytes       requested payload size (<= class capacity)
 *   +12 u32 ref_count           number of TRAILING payload u32 words that
 *                               are handles (the object's exact ref map)
 *   +16 payload...
 *
 * When the block is free, the first payload u32 is the next-free-block
 * offset (MOBAGEN_MEMORY_NULL_OFFSET terminates) and the header's free bit
 * is set; the remaining header fields stay valid.
 *
 * Handles (stable indirection, slot-based + generational, 32 bits):
 *
 *   [31:16] generation  starts at 1, bumps every time the slot is freed,
 *                       so a stale handle from a previous occupant resolves
 *                       to null (generation 0 never exists: slot 0 gen 0
 *                       would encode as the null-handle sentinel)
 *   [15:0]  slot index  into the region-resident handle table
 *
 * Objects never move; a handle resolves to the same payload address for the
 * object's whole lifetime. Payload ref words are plain u32 handle values the
 * tracing mark phase resolves with a double-generation read (see
 * memory_manager.cpp); they must be initialized to
 * MOBAGEN_MEMORY_NULL_HANDLE by the allocator and may be overwritten by the
 * owner.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_MEMORY_OBJECT_ABI_VERSION UINT32_C(1)

/* "MGO1" */
#define MOBAGEN_MEMORY_OBJECT_MAGIC_V1 UINT32_C(0x4D474F31)

#define MOBAGEN_MEMORY_OBJECT_HEADER_BYTES UINT32_C(16)

/* Terminator for region offsets (free-list links, handle-table entries). */
#define MOBAGEN_MEMORY_NULL_OFFSET UINT32_C(0xFFFFFFFF)

#define MOBAGEN_MEMORY_NULL_HANDLE UINT32_C(0x00000000)

/* flags byte 0: size class; bit 8 mark; bit 9 free; rest reserved zero. */
#define MOBAGEN_MEMORY_FLAG_SIZE_CLASS_MASK UINT32_C(0x000000FF)
#define MOBAGEN_MEMORY_FLAG_MARK_BIT UINT32_C(0x00000100)
#define MOBAGEN_MEMORY_FLAG_FREE_BIT UINT32_C(0x00000200)

/*
 * Size classes: class i serves payloads of at most (16 << i) bytes,
 * i = 0 .. MOBAGEN_MEMORY_SIZE_CLASS_COUNT-1. Block stride for class i is
 * header + (16 << i), always 16-byte aligned. Payloads larger than the top
 * class capacity are unsupported in v1 (loud failure, never silent
 * truncation).
 */
#define MOBAGEN_MEMORY_SIZE_CLASS_COUNT 11
#define MOBAGEN_MEMORY_MIN_ALIGN 16

#ifdef __cplusplus
} /* extern "C" */

#  include <cstdint>

namespace mobagen {
  namespace memory {

    inline constexpr std::uint32_t object_abi_version = MOBAGEN_MEMORY_OBJECT_ABI_VERSION;
    inline constexpr std::uint32_t object_magic_v1 = MOBAGEN_MEMORY_OBJECT_MAGIC_V1;
    inline constexpr std::uint32_t object_header_bytes = MOBAGEN_MEMORY_OBJECT_HEADER_BYTES;
    inline constexpr std::uint32_t null_offset = MOBAGEN_MEMORY_NULL_OFFSET;
    inline constexpr std::uint32_t null_handle = MOBAGEN_MEMORY_NULL_HANDLE;
    inline constexpr std::uint32_t size_class_count = MOBAGEN_MEMORY_SIZE_CLASS_COUNT;

    inline constexpr std::uint32_t size_class_capacity(std::uint32_t size_class) { return 16U << size_class; }

    /* Smallest class whose capacity fits payload_bytes; size_class_count if none. */
    inline constexpr std::uint32_t size_class_for(std::uint32_t payload_bytes) {
      for (std::uint32_t i = 0; i < size_class_count; ++i) {
        if (payload_bytes <= size_class_capacity(i)) return i;
      }
      return size_class_count;
    }

    inline constexpr std::uint32_t size_class_block_bytes(std::uint32_t size_class) { return object_header_bytes + size_class_capacity(size_class); }

    inline constexpr std::uint32_t max_payload_bytes() { return size_class_capacity(size_class_count - 1U); }

    /* Handle encode/decode (slot-based + 16-bit generation). */
    inline constexpr std::uint32_t handle_slot(std::uint32_t handle) { return handle & 0xFFFFU; }
    inline constexpr std::uint32_t handle_generation(std::uint32_t handle) { return handle >> 16; }
    inline constexpr bool handle_is_null(std::uint32_t handle) { return handle == null_handle; }
    inline constexpr std::uint32_t make_handle(std::uint32_t generation, std::uint32_t slot) { return (generation << 16) | (slot & 0xFFFFU); }

  }  // namespace memory
}  // namespace mobagen
#endif

#endif /* MOBAGEN_MEMORY_OBJECT_HEADER_H */
