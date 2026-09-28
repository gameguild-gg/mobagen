#ifndef MOBAGEN_MEMORY_SHIM_H
#define MOBAGEN_MEMORY_SHIM_H

/*
 * Mobagen memory shim (todo 12) — the ONLY platform seam of the memory
 * core (region allocator + tracing mark-sweep GC).
 *
 * Everything platform-flavored reaches the algorithm code through the
 * MobagenMemoryShim vtable below:
 *
 *   - region acquire      shim->region_acquire()   — where the bytes live
 *   - atomic u32 ops      atomic_load/store/cas    — mark bits + free lists
 *   - memory fence        atomic_fence             — publish ordering
 *   - wait / notify       word_wait / word_notify  — host threads park at the
 *                                                     GC barrier
 *   - monotonic clock     now_ns                   — quiesce timeouts
 *   - tracing callback    trace(message)           — loud failures
 *
 * Todo 13 implements this over the WAMR shared heap region
 * (WamrBackend::shared_heap_region(), DO NOT modify that accessor) +
 * std::atomic + pthread mutex/condvar. Todo 14 implements it over a
 * SharedArrayBuffer + Atomics (with an isolated single-context fallback).
 * The algorithm code NEVER includes pthread.h / emscripten.h / WAMR — it
 * includes this header and object_header.h only.
 *
 * ATOMIC WORDS ARE REGION-RESIDENT u32s. The shim owns a per-engine-instance
 * allocator_word_t object; algorithm code passes it to every atomic op. This
 * is exactly what SAB/Atomics need (Atomics.load/compareExchange operate on
 * an Int32Array VIEW over the shared buffer) and exactly what native needs
 * (std::atomic_ref-style access to the mapped shared heap). A shim's words
 * must be SHARED across all participants of one region.
 *
 * ---- GC safe points (the complete set) ----
 *
 * 1. Allocation. alloc() reads the GC epoch word through the shim. If a GC
 *    is in flight (epoch odd), the allocating thread parks in word_wait on
 *    the GC word until the epoch flips even, then retries. Allocation is
 *    therefore always observed at a consistent heap.
 *
 * 2. Host-side invoke frames (todo 13 native policy). Managed host threads
 *    that enter the GC via collect() first bump the epoch (odd), then park
 *    on the host-park word. Threads not allocating simply observe the
 *    flipped epoch at their next allocation.
 *
 * 3. Managed guest threads. For modules with manifest threads: managed, the
 *    module exports mobagen_module_thread_quiesce_v1; the spawn host import
 *    (mobagen_thread_spawn_v1) registers the thread's parking word with the
 *    memory manager. Stop-the-world = request -> per-registered-thread
 *    handshake (quiesce export parks the guest thread on ITS OWN word) ->
 *    mark/sweep -> release (notify all). Quiesce timeout = loud abort issue,
 *    NEVER mark a live heap.
 *
 * threads: none modules cannot run outside an invoke frame by construction
 * (host resolves no thread-spawning import for them); allocation (safe
 * point 1) covers everything they can do.
 *
 * ---- Stop-the-world sequence (verbatim policy, todo 12) ----
 *
 *   request GC (epoch -> odd)
 *     -> for each managed guest thread tracked by this manager: invoke its
 *        quiesce handshake; it parks the guest thread at a safe point
 *     -> mark from registered roots (handle table in-registry slots)
 *     -> sweep unmarked blocks back to their size-class free lists
 *     -> release (epoch -> even; notify all parked words)
 *
 * Signal-based suspension is never used. Spin-waiting is never used (the
 * shim's word_wait must block).
 *
 * ---- Control block (region bytes 0..64; todo 13 final contract) ----
 *
 *   +0  u32 magic "MMG1"
 *   +4  u32 epoch (bit0 = GC in flight; shim-atomic, CAS-guarded)
 *   +8  u32 host_park (wakes host allocators parked at the barrier)
 *   +12 u32 live_objects
 *   +16 u32 in_flight (shim-atomic): the mutator bracket — incremented
 *       (CAS loop; the vtable has no fetch_add) by alloc() around its
 *       mutating section, decremented on exit; the collector drains it to
 *       0 (word_wait on the word; the 1->0 transition notifies) before
 *       marking so it never sweeps a half-built object.
 *   +20 u32 total_allocs, +24 u32 total_frees (diagnostics)
 *
 * ---- Collect sequence (todo 13, micro-task B: quiesce-first) ----
 *
 *   1. quiesce managed guest threads FIRST (before ANY epoch flip). On
 *      timeout/failed handshake: release already-parked guests, abort
 *      loudly (QuiesceTimeout/QuiesceFailed issue + trace). The epoch was
 *      never flipped, so the heap is untouched and gc_in_flight() stays
 *      false — never mark a live heap.
 *   2. CAS the epoch even->odd (park-and-retry defensively if odd). Bump
 *      host_park once so parked allocators' word_wait predicates change.
 *      If the epoch moved since step 1 (a concurrent collector completed
 *      and its release woke our guests), re-quiesce before marking.
 *   3. Drain the in_flight bracket (+16) to zero.
 *   4. Mark from registered roots (handle-table in-registry slots).
 *   5. Sweep unmarked blocks back to their size-class free lists; release
 *      handle slots with a generation bump.
 *   6. Release: wake parked guest threads (release_fn), then epoch -> even
 *      + host_park bump + word_notify_all for host parkers.
 *
 * ---- Managed guest loop protocol + self-quiesce rule (todo 13) ----
 *
 * A threads:managed module's guest thread runs:
 *
 *   check request flag -> park on OWN word -> release -> alloc -> ...
 *
 * The parking word is a region-resident u32 the guest owns (typically
 * payload word 0 of a rooted block); park = word_wait(own_word, cur, -1),
 * release (host-side release_fn) = value bump + word_notify_all. The
 * C-callable {quiesce_fn, release_fn} pair registered via
 * register_guest_thread is what the platform layer wraps around the
 * module's mobagen_module_thread_quiesce_v1 export. quiesce_fn sets the
 * request flag and waits (bounded, via shim now_ns()) for the parked
 * state; its budget must be SMALLER than the manager's so the guest's
 * deadline surfaces as the abort reason.
 *
 * SELF-QUIESCE RULE: allocation is itself a safe point, so a guest whose
 * OWN alloc() triggers the synchronous auto-collect would deadlock waiting
 * for itself to park. quiesce_fn must recognize this case (caller thread
 * id == the guest's thread id) and return MOBAGEN_QUIESCE_OK immediately:
 * the thread is inside alloc(), which is a safe point by construction.
 *
 * ---- T14 mirror notes (web shim over SAB + Atomics) ----
 *
 * - Bucket design (native shim): 16 mutex/condvar buckets keyed by
 *   (word_offset >> 2) & 15. word_wait re-checks the word under the bucket
 *   mutex before sleeping (predicate: value != current_value) so a notify
 *   that lands between check and sleep is never lost; word_notify_all
 *   broadcasts under the same bucket mutex. The web mirror is one Waiter
 *   list per word index with Atomics.wait/notify — same predicate shape,
 *   index = opaque >> 2 of the Int32Array view.
 * - in_flight drain: the collector word_waits on the in_flight word (not a
 *   timed spin); the 1->0 transition word_notify_alls. Web mirror:
 *   Atomics.wait on the in_flight element with the same notify-on-zero
 *   from the decrementing side (CAS loops both sides — no fetch_add in the
 *   vtable, and Atomics.add could not be expressed through it either).
 * - Park predicate re-read: park_until_epoch_even re-reads `cur` (the
 *   host_park value) every loop iteration before word_wait — a bumped
 *   value from a PREVIOUS release must not park a thread on a stale
 *   predicate (hot-spin bug class). The guest loop protocol mirrors this:
 *   the guest re-reads its request sequence each iteration and consumes
 *   bumps only through release_fn.
 */

#include "object_header.h"

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Handle for one atomic u32 word. CONTRACT (both platform shims must honor
 * it): opaque is the BYTE OFFSET of a 4-aligned u32 inside the acquired
 * region; atomic_load/store/cas and word_wait/word_notify_all apply to the
 * u32 at region_base + opaque. Native (todo 13) implements this with
 * atomic access into the WAMR shared-heap bytes; web (todo 14) implements
 * it as Atomics.* on the Int32Array view of the SharedArrayBuffer (index =
 * opaque >> 2). Algorithm code constructs words from region offsets; it
 * never caches raw pointers across threads.
 */
typedef struct MobagenAllocatorWord {
  uintptr_t opaque;
} MobagenAllocatorWord;

/* Region view: raw bytes shared by all participants. 16-byte base alignment required. */
typedef struct MobagenRegionView {
  uint8_t* bytes;
  size_t size;
} MobagenRegionView;

/* Region acquire outcome. ok=0 means region+word are valid. */
typedef struct MobagenRegionResult {
  int ok;
  MobagenRegionView region;
  MobagenAllocatorWord word; /* the atomic word for mark-bit / GC-epoch ops */
  char failure[96];
} MobagenRegionResult;

/* Quiesce handshake outcome for one managed guest thread. */
typedef enum MobagenQuiesceStatus { MOBAGEN_QUIESCE_OK = 0, MOBAGEN_QUIESCE_TIMEOUT = 1, MOBAGEN_QUIESCE_FAILED = 2 } MobagenQuiesceStatus;

/* vtable: implement ALL of these. See file header for the contract. */
typedef struct MobagenMemoryShim {
  void* user_data;

  /* Acquire (or attach to) the shared region with at least minimum_bytes
   * usable, 16-byte base aligned. Called once per manager; the returned
   * region may be larger than requested (e.g. page-rounded). */
  MobagenRegionResult (*region_acquire)(void* user_data, size_t minimum_bytes);

  /* Atomic u32 ops on region-resident words. Every field of the object
   * area (mark bit, free-bit, list links, ref words, handle table slots)
   * accessed cross-thread goes through these. */
  uint32_t (*atomic_load)(void* user_data, MobagenAllocatorWord word);
  void (*atomic_store)(void* user_data, MobagenAllocatorWord word, uint32_t value);
  int (*atomic_cas)(void* user_data, MobagenAllocatorWord word, uint32_t expected, uint32_t desired);
  void (*atomic_fence)(void* user_data); /* full fence (seq_cst) */

  /* Blocking wait until the word's value differs from current_value, or
   * timeout_ms elapses. Returns the observed value. MUST block (no spin);
   * timeout <= 0 = wait forever. */
  uint32_t (*word_wait)(void* user_data, MobagenAllocatorWord word, uint32_t current_value, int32_t timeout_ms);
  /* Wake every thread parked on word. */
  void (*word_notify_all)(void* user_data, MobagenAllocatorWord word);

  /* Monotonic nanoseconds since an arbitrary epoch (quiesce timeouts). */
  uint64_t (*now_ns)(void* user_data);

  /* Best-effort trace sink for loud failures (may be NULL). */
  void (*trace)(void* user_data, const char* message);
} MobagenMemoryShim;

#ifdef __cplusplus
} /* extern "C" */

namespace mobagen {
  namespace memory {

    /* C++ wrapper around the vtable; null-shim tolerance built in. */
    class Shim {
    public:
      Shim() = default;
      explicit Shim(const MobagenMemoryShim& vtable) : shim_(vtable) {}

      [[nodiscard]] bool valid() const noexcept { return shim_.region_acquire != nullptr; }
      [[nodiscard]] const MobagenMemoryShim& raw() const noexcept { return shim_; }
      [[nodiscard]] void* user_data() const noexcept { return shim_.user_data; }

      [[nodiscard]] MobagenRegionResult region_acquire(std::size_t minimum_bytes) const noexcept {
        return shim_.region_acquire(shim_.user_data, minimum_bytes);
      }

      [[nodiscard]] std::uint32_t atomic_load(MobagenAllocatorWord w) const noexcept { return shim_.atomic_load(shim_.user_data, w); }
      void atomic_store(MobagenAllocatorWord w, std::uint32_t v) const noexcept { shim_.atomic_store(shim_.user_data, w, v); }
      [[nodiscard]] bool atomic_cas(MobagenAllocatorWord w, std::uint32_t expected, std::uint32_t desired) const noexcept {
        return shim_.atomic_cas(shim_.user_data, w, expected, desired) != 0;
      }
      void atomic_fence() const noexcept { shim_.atomic_fence(shim_.user_data); }

      [[nodiscard]] std::uint32_t word_wait(MobagenAllocatorWord w, std::uint32_t current, std::int32_t timeout_ms) const noexcept {
        return shim_.word_wait(shim_.user_data, w, current, timeout_ms);
      }
      void word_notify_all(MobagenAllocatorWord w) const noexcept { shim_.word_notify_all(shim_.user_data, w); }

      [[nodiscard]] std::uint64_t now_ns() const noexcept { return shim_.now_ns(shim_.user_data); }

      void trace(const char* message) const noexcept {
        if (shim_.trace != nullptr) shim_.trace(shim_.user_data, message);
      }

    private:
      MobagenMemoryShim shim_{};
    };

  }  // namespace memory
}  // namespace mobagen
#endif

#endif /* MOBAGEN_MEMORY_SHIM_H */
