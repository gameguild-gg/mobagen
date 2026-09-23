#pragma once

/*
 * Mobagen memory manager + GC core (todo 12) — platform-neutral.
 *
 * Region-based allocator (size-class free lists over one region) + tracing
 * mark-sweep GC with handle-based roots. See shim.hpp for the platform seam
 * contract and the full safe-point set; object_header.h for the object
 * format. This code compiles clean under emscripten (no pthread/WAMR/SAB
 * includes; ALL platform primitives arrive through the shim).
 *
 * Threading policy (plan todo 12, verbatim): v1 GC quiesce = host-driven
 * stop-the-world at host-known safe points, valid ONLY because no
 * guest-owned thread runs outside an invoke frame. threads:none modules get
 * no thread-spawning import resolved (spawning impossible by construction);
 * threads:managed modules spawn via mobagen_thread_spawn_v1 and MUST export
 * mobagen_module_thread_quiesce_v1 — stop-the-world invokes that handshake
 * per live guest thread (tracked below) before marking. Quiesce timeout =
 * loud abort issue, never mark a live heap.
 */

#include "object_header.h"
#include "shim.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mobagen {
  namespace memory {

    inline constexpr std::uint32_t control_bytes = 64; /* epoch + counters, shim-atomic */
    inline constexpr std::uint32_t handle_table_start = control_bytes;
    inline constexpr std::uint32_t handle_stride = 8; /* u32 slot: [0]=block offset, [1..] pad */
    inline constexpr std::uint32_t default_handle_slots = 4096;
    inline constexpr std::uint32_t min_region_bytes = 64U * 1024U;

    enum class MemoryIssueCode : std::uint8_t {
      None,
      ShimInvalid,
      RegionTooSmall,
      RegionMisaligned,
      OutOfMemory,
      TooLarge,
      InvalidHandle,
      DoubleFree,
      QuiesceTimeout,
      QuiesceFailed,
      Internal,
    };

    struct MemoryIssue {
      MemoryIssueCode code{MemoryIssueCode::None};
      std::string message;
    };

    struct CollectStats {
      std::uint32_t marked{0};
      std::uint32_t swept{0};
      std::uint32_t quiesced_threads{0};
    };

    /* Registered managed guest thread (threads:managed modules only). The
     * quiesce_fn is the module's mobagen_module_thread_quiesce_v1 export
     * adapted into a C callback by the platform layer; the GC invokes it
     * and it must park the guest thread at a safe point (word_wait on its
     * parking word) and return; release_fn wakes it. user_data flows to
     * both. Timeout budget applies per thread. */
    struct GuestThread {
      std::uint32_t id{0};
      void* user_data{nullptr};
      /* Returns MOBAGEN_QUIESCE_OK once the guest thread is parked, or
       * MOBAGEN_QUIESCE_TIMEOUT/FAILED. May itself word_wait. */
      int (*quiesce_fn)(void* user_data, std::int32_t timeout_ms);
      /* Wakes the parked guest thread (release step). */
      void (*release_fn)(void* user_data);
    };

    /* The memory manager. One per engine instance; per-instance state, no
     * process globals. Host usage:
     *   MemoryManager mgr(shim);
     *   mgr.init(region_size_bytes);          // carve region + handle table
     *   Handle h = mgr.register_root(mgr.alloc(payload, ref_count));
     *   ...                                    // alloc() is a safe point
     *   mgr.collect(&stats, &issue);          // stop-the-world
     *   mgr.unregister_root(h);
     */
    class MemoryManager {
    public:
      using Handle = std::uint32_t; /* value = object_header.h encoding */

      explicit MemoryManager(Shim shim);
      ~MemoryManager();

      MemoryManager(const MemoryManager&) = delete;
      MemoryManager& operator=(const MemoryManager&) = delete;
      MemoryManager(MemoryManager&&) = delete;
      MemoryManager& operator=(MemoryManager&&) = delete;

      /* Carves the acquired region: control block, handle table, per-class
       * chunk areas, builds free lists. Region must be >= min_region_bytes
       * and 16-byte aligned. */
      [[nodiscard]] bool init(std::size_t region_request_bytes, MemoryIssue* out_issue = nullptr);
      /* --- Allocation (SAFE POINT: parks while a GC epoch is in flight) --- */

      /* Allocates payload_bytes with ref_count trailing handle words
       * (initialized null). Returns null offset on failure. */
      [[nodiscard]] std::uint32_t alloc(std::uint32_t payload_bytes, std::uint32_t ref_count, MemoryIssue* out_issue = nullptr);
      void free(std::uint32_t block_offset, MemoryIssue* out_issue = nullptr);

      /* --- Handles (stable, slot+generation; see object_header.h) --- */

      /* Binds a fresh handle to an allocated block; deterministic and
       * side-effect-free apart from slot allocation: two managers with the
       * same allocation sequence produce identical handle values. */
      [[nodiscard]] std::uint32_t handle_for(std::uint32_t block_offset, MemoryIssue* out_issue = nullptr);
      /* Resolves handle -> current block offset, or null_offset if the
       * generation is stale / slot empty. */
      [[nodiscard]] std::uint32_t handle_resolve(Handle handle) const noexcept;
      /* Payload access. block_end = base + object_header_bytes. */
      [[nodiscard]] std::uint8_t* payload(std::uint32_t block_offset) noexcept;
      [[nodiscard]] const std::uint8_t* payload(std::uint32_t block_offset) const noexcept;
      [[nodiscard]] std::uint32_t payload_bytes(std::uint32_t block_offset) const noexcept;

      /* --- Roots (deterministic registration API) --- */

      /* Marks the handle-table slot in-registry: the slot's offset is kept
       * and the object stays reachable even if the application drops its
       * own reference. Returns the handle. Idempotent per handle. */
      [[nodiscard]] std::uint32_t register_root(Handle handle, MemoryIssue* out_issue = nullptr);
      void unregister_root(Handle handle) noexcept;
      [[nodiscard]] bool is_root(Handle handle) const noexcept;
      [[nodiscard]] std::size_t root_count() const noexcept;
      /* Roots are addressable by index in registration order (deterministic
       * iteration for tests + mark order). */
      [[nodiscard]] std::uint32_t root_at(std::size_t index) const noexcept;

      /* --- GC (stop-the-world; see shim.hpp safe points) --- */

      [[nodiscard]] bool collect(CollectStats* out_stats = nullptr, MemoryIssue* out_issue = nullptr);
      /* true while a collect() is between epoch-flip and release. */
      [[nodiscard]] bool gc_in_flight() const noexcept;

      /* --- Managed guest threads (threads:managed modules) --- */

      /* Registers a live guest thread (its quiesce handshake + release).
       * Returns its id. The shim-side spawn import must call this. */
      [[nodiscard]] std::uint32_t register_guest_thread(GuestThread thread, MemoryIssue* out_issue = nullptr);
      void unregister_guest_thread(std::uint32_t id) noexcept;
      [[nodiscard]] std::size_t guest_thread_count() const noexcept;
      /* Stop-the-world handshake only (what collect() runs before marking).
       * Exposed for tests + todo 13 to drive the policy explicitly. */
      [[nodiscard]] bool quiesce_guest_threads(std::uint32_t timeout_ms, CollectStats* out_stats = nullptr, MemoryIssue* out_issue = nullptr);
      /* Wakes every parked guest thread (the release step of stop-the-world). */
      void release_guest_threads() noexcept;

      /* --- Introspection (tests) --- */

      [[nodiscard]] std::size_t region_size() const noexcept;
      /* Free-list depth for a size class: baseline assertion after collect. */
      [[nodiscard]] std::uint32_t free_count(std::uint32_t size_class) const noexcept;
      [[nodiscard]] std::uint32_t live_objects() const noexcept;
      [[nodiscard]] std::uint8_t* region_base() noexcept;

    private:
      struct SizeClassInfo {
        std::uint32_t chunk_offset{null_offset};
        std::uint32_t chunk_bytes{0};
        std::uint32_t block_count{0};
        std::uint32_t head{null_offset}; /* free-list head offset */
        std::uint32_t free_blocks{0};
      };

      Shim shim_;
      std::uint8_t* base_{nullptr};
      std::size_t region_size_{0};
      MobagenAllocatorWord epoch_word_{};
      MobagenAllocatorWord host_park_word_{};
      MobagenAllocatorWord in_flight_word_{}; /* control +16: mutators inside the bracket */
      std::uint32_t handle_slots_{0};
      std::uint32_t handles_in_use_{0};
      std::vector<SizeClassInfo> classes_{size_class_count};
      std::vector<Handle> roots_; /* deterministic registration order */
      std::vector<GuestThread> guest_threads_;
      std::uint32_t next_guest_thread_id_{1};
      /* Manager mutex: protects classes_, handle slots, roots_, guest_threads_
       * vectors. NEVER held across parking, word_wait, quiesce callbacks,
       * drain, or blocking collect steps. */
      mutable std::mutex host_mu_;

      /* Raw u32 access inside the region (single-threaded setup / quiesced
       * phases); cross-thread fields go through shim atomics. */
      [[nodiscard]] std::uint32_t rd32(std::uint32_t offset) const noexcept;
      void wr32(std::uint32_t offset, std::uint32_t v) noexcept;
      bool park_until_epoch_even(MemoryIssue* out_issue);
      void mark_handle(Handle h, std::uint32_t* out_marked);
      /* in-flight bracket on the +16 word (vtable has no fetch_add: CAS loops). */
      void in_flight_enter();
      void in_flight_leave();
    };

  }  // namespace memory
}  // namespace mobagen
