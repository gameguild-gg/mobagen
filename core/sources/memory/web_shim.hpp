#ifndef MOBAGEN_MEMORY_WEB_SHIM_H
#define MOBAGEN_MEMORY_WEB_SHIM_H

/*
 * Web memory shim (todo 14) — MobagenMemoryShim vtable over the emscripten
 * linear-memory heap + JS Atomics, compiled ONLY under EMSCRIPTEN (both web
 * variants from todo 5; the native shim is todo 13 and is NOT compiled
 * here). Structural mirror of native_shim.hpp.
 *
 * ---- Where the region lives (and why not a fresh SAB) ----
 *
 * The algorithm code (memory_manager.cpp) accesses the region through the
 * raw `MobagenRegionView.bytes` pointer with plain loads/stores (rd32/wr32)
 * and only routes ATOMIC WORDS through the vtable. A `new SharedArrayBuffer`
 * allocated in JS is not addressable from wasm — there is no linear-memory
 * pointer for it — so the region is carved from the EMSCRIPTEN HEAP via
 * aligned_alloc. In the SHARED variant (-sUSE_PTHREADS=1 -sSHARED_MEMORY=1,
 * MOBAGEN_WEB_SHARED=1) that heap IS a SharedArrayBuffer (the wasm memory is
 * constructed shared:true; the smoke bundle check in scripts/smoke_web.mjs
 * relies on exactly that), so Atomics.* on the live HEAP32 Int32Array view
 * is cross-agent-visible and wait/notify are real. In the ISOLATED variant
 * (MOBAGEN_WEB_SHARED undefined — todo 6's probe picked a SAB-less context)
 * the heap is a plain ArrayBuffer: single-context mode, atomics degrade to
 * plain u32 access, wait/notify degrade to no-op-yield (below).
 *
 * ---- Word index contract (shim.hpp, verbatim) ----
 *
 * MobagenAllocatorWord.opaque is the BYTE OFFSET of a 4-aligned u32 inside
 * the acquired region; on wasm32 the region pointer's integer value IS its
 * linear-memory byte offset, so the Int32Array index of a word is
 * (region_base + opaque) >> 2 — computed in C++ and handed to the EM_JS
 * shims as a plain u32 index. The EM_JS bodies always reference the LIVE
 * HEAP32 global so memory growth (which swaps the JS views) never leaves
 * them pointing at a detached buffer.
 *
 * ---- THREADING RULE (plan todo 14, review rr-dlap-2 / Oracle M3) ----
 *
 * Firefox and Safari FORBID Atomics.wait on the MAIN thread (it throws even
 * in cross-origin-isolated contexts; Chrome permits it only when isolated).
 * Blocking waits therefore run on WORKER AGENTS ONLY; the host main thread
 * participates in stop-the-world through bounded polling. Enforcement, both
 * layers:
 *   1. Before every Atomics.wait the JS side classifies the current agent
 *      (browser WorkerGlobalScope, or node worker_threads !isMainThread; a
 *      force flag exists for tests). Non-worker agents never reach
 *      Atomics.wait — they take the bounded-poll fallback directly.
 *   2. Every Atomics.wait call site is wrapped in a JS capability try/catch
 *      that maps a throw to the same bounded-poll fallback.
 * The fallback polls the word roughly every 1ms up to the timeout (an
 * infinite timeout keeps polling at the capped interval — the collector's
 * quiesce deadlines guarantee release; it never busy-spins without
 * pacing). Pacing uses Atomics.wait on a private scratch SAB where that is
 * legal (node, browser workers, isolated-main Chrome) and a Date-bounded
 * CPU slice as the last resort on main threads that forbid Atomics.wait
 * entirely — loud comment in the JS body.
 *
 * Counters (globalThis.__mobagenWebShimStats, readable via the static
 * accessors below) prove the rule mechanically:
 *   blocking_waits()            — executed Atomics.wait calls (worker path)
 *   main_thread_blocking_waits()— Atomics.wait executed on a non-worker
 *                                 agent; structurally ZERO, asserted by the
 *                                 node smoke (a future regression that drops
 *                                 the agent guard trips this counter)
 *   poll_fallback_calls()       — waits served by the bounded-poll path
 *
 * ---- Isolated fallback semantics ----
 *
 * MOBAGEN_WEB_SHARED undefined: single-context region (plain ArrayBuffer
 * heap), atomics = plain u32 access (single agent — identical semantics, no
 * SAB dependency), fence = compiler-only ordering (nothing to publish
 * across), word_wait returns the observed value immediately (no-op-yield:
 * a single agent has nothing to block on — the epoch can only be odd while
 * that same agent is inside collect(), so a park can never be observed),
 * word_notify_all = no-op. The first word_wait traces ONE loud degradation
 * note. The ALGORITHM code (memory_manager.cpp) is byte-identical in both
 * variants; only this shim differs.
 *
 * T26 call site (todo 14 integration):
 *   auto s = make_web_shim();
 *   MemoryManager m(s->shim());
 */

#include "memory/shim.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mobagen {
  namespace memory {

    class WebShim {
    public:
      /* Owns a 16-byte-aligned region carved from the emscripten heap,
       * initially 256 KiB, grown on demand in region_acquire. */
      WebShim();
      ~WebShim();

      WebShim(const WebShim&) = delete;
      WebShim& operator=(const WebShim&) = delete;

      /* vtable with this as user_data (see shim.hpp for the contract). */
      [[nodiscard]] Shim shim() const noexcept;

      /* Best-effort trace sink log (loud failures + the isolated-mode
       * degradation note). */
      [[nodiscard]] const std::vector<std::string>& trace_log() const noexcept;

      /* true when compiled into the shared variant (MOBAGEN_WEB_SHARED). */
      [[nodiscard]] static bool shared_variant() noexcept;

      /* JS-side counters (see the threading-rule block above). Bundle-wide:
       * the browser-backend smoke asserts them after its memory scenario. */
      [[nodiscard]] static std::uint32_t stats_blocking_waits() noexcept;
      [[nodiscard]] static std::uint32_t stats_main_thread_blocking_waits() noexcept;
      [[nodiscard]] static std::uint32_t stats_poll_fallback_calls() noexcept;
      /* Test hook: classify this agent as the main thread regardless of the
       * real topology (exercises the bounded-poll path deterministically). */
      static void set_force_main_thread(bool forced) noexcept;

    private:
      MobagenRegionResult region_acquire(std::size_t minimum_bytes);
      std::uint32_t atomic_load(MobagenAllocatorWord word) noexcept;
      void atomic_store(MobagenAllocatorWord word, std::uint32_t value) noexcept;
      int atomic_cas(MobagenAllocatorWord word, std::uint32_t expected, std::uint32_t desired) noexcept;
      void atomic_fence() noexcept;
      std::uint32_t word_wait(MobagenAllocatorWord word, std::uint32_t current_value, std::int32_t timeout_ms) noexcept;
      void word_notify_all(MobagenAllocatorWord word) noexcept;
      std::uint64_t now_ns() noexcept;
      void trace(const char* message);

      /* Int32Array index of a region word: (region_base + opaque) >> 2. */
      [[nodiscard]] std::uint32_t word_index(MobagenAllocatorWord word) const noexcept;

      std::uint8_t* region_{nullptr};
      std::size_t region_size_{0};
      bool isolated_wait_note_{false};
      std::mutex trace_mu_;
      std::vector<std::string> trace_log_;
      MobagenMemoryShim vtable_{};
    };

    /* Free factory (standalone web shim). */
    [[nodiscard]] std::unique_ptr<WebShim> make_web_shim();

  }  // namespace memory
}  // namespace mobagen

#endif /* MOBAGEN_MEMORY_WEB_SHIM_H */
