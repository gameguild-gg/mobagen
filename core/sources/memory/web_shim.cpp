#include "memory/web_shim.hpp"

/*
 * Web memory shim (todo 14) — MobagenMemoryShim over the emscripten
 * linear-memory heap + JS Atomics. Emscripten-only TU (both variants); the
 * native twin is native_shim.cpp (todo 13). See web_shim.hpp for the region
 * model, the word-index contract, and the threading rule.
 *
 * clang-format note: the EM_JS JS bodies below deliberately use only
 * clang-format-safe JS (loose ==/!=, // comments) — the formatter rewraps
 * tokens as C++ and would corrupt !== / === into invalid JavaScript.
 */

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>

namespace mobagen {
  namespace memory {

    namespace {

      constexpr std::size_t kBaseAlign = 16;             /* region base alignment (shim.hpp contract) */
      constexpr std::size_t kInitialSize = 256U * 1024U; /* initial region */
      constexpr std::size_t kGrowAlign = 64U * 1024U;    /* growth quantum */

      std::size_t round_up(std::size_t value, std::size_t quantum) noexcept {
        return (value + quantum - 1) / quantum * quantum;
      }

      /*
       * ---- JS primitives (shared variant) ----
       *
       * Atomics.* operate on the LIVE HEAP32 Int32Array view: under
       * -sUSE_PTHREADS=1 -sSHARED_MEMORY=1 the wasm memory is constructed
       * shared:true, so HEAP32's buffer IS the SharedArrayBuffer and these
       * are real cross-agent atomics. Referencing the live global (never a
       * cached view) keeps the indices valid across ALLOW_MEMORY_GROWTH
       * swaps. The index is computed C++-side: (region_base + opaque) >> 2.
       *
       * EM_JS_DEPS is not needed: HEAP32 is a plain module-scope JS var
       * (same usage as browser_wasm_backend.cpp), not a $-prefixed runtime
       * library symbol.
       */
      EM_JS(int, mobagen_web_shim_atomics_load, (unsigned int index), { return Atomics.load(HEAP32, index); });

      EM_JS(void, mobagen_web_shim_atomics_store, (unsigned int index, int value), { Atomics.store(HEAP32, index, value); });

      EM_JS(int, mobagen_web_shim_atomics_cas, (unsigned int index, int expected, int desired), {
        // compareExchange returns the OLD value; map to the vtable's boolean.
        return Atomics.compareExchange(HEAP32, index, expected, desired) == expected ? 1 : 0;
      });

      // Fence: a seq_cst Atomics.load on word 0 (read-only — never mutates
      // linear memory) is a full barrier per the JS memory model.
      EM_JS(void, mobagen_web_shim_atomics_fence, (void), { Atomics.load(HEAP32, 0); });

      EM_JS(int, mobagen_web_shim_notify, (unsigned int index), {
        // Legal from ANY agent (only Atomics.wait is agent-restricted).
        try {
          Atomics.notify(HEAP32, index);
        } catch (e) {
          // non-shared buffer (should be impossible here) — degrade silently;
          // single-agent semantics do not need the wakeup
        }
        return 0;
      });

      /*
       * word_wait — the THREADING RULE lives here (plan todo 14, review
       * rr-dlap-2 / Oracle M3, verbatim policy):
       *
       *   Firefox and Safari FORBID Atomics.wait on the MAIN thread (it
       *   throws, even in cross-origin-isolated contexts; Chrome permits it
       *   only when isolated) — blocking waits run on WORKER agents only;
       *   the host main thread participates via bounded polling; every
       *   Atomics.wait call site is wrapped in a capability try/catch that
       *   maps a throw to the bounded-poll fallback.
       *
       * Counters (globalThis.__mobagenWebShimStats) make the rule testable:
       * blocking = executed Atomics.wait calls; main = waits that executed
       * on a non-worker agent (structurally ZERO — a tripwire for a future
       * regression that drops the agent guard); poll = waits served by the
       * bounded-poll path.
       *
       * Predicate shape (shim.hpp contract): return when the word's value
       * differs from `expected`, or the timeout elapses; return the observed
       * value. Atomics.wait itself re-validates `expected` atomically before
       * sleeping (futex semantics), so a notify that lands between the entry
       * read and the sleep is never lost — the native bucket-mutex re-check,
       * for free.
       */
      EM_JS(int, mobagen_web_shim_wait, (unsigned int index, int expected, int timeout_ms), {
        var stats = (globalThis.__mobagenWebShimStats = globalThis.__mobagenWebShimStats || {blocking : 0, main : 0, poll : 0});
        var v = HEAP32[index] | 0;
        if (v != expected) return v;

        function isWorkerAgent() {
          var cached = globalThis.__mobagenWorkerAgent;
          if (cached != undefined) return cached;
          var ok = false;
          try {
            // Browser workers (also DedicatedWorkerGlobalScope subclasses).
            if (typeof WorkerGlobalScope != "undefined" && typeof self != "undefined" && self instanceof WorkerGlobalScope) ok = true;
          } catch (e) {
          }
          if (!ok) {
            try {
              // node worker_threads: a worker agent (SAB + Atomics.wait legal
              // there; the smoke runner boots bundles exactly this way).
              if (typeof process != "undefined" && process.versions && process.versions.node && typeof require == "function") {
                var wt = require("node:worker_threads");
                if (wt && wt.isMainThread == false) ok = true;
              }
            } catch (e) {
            }
          }
          globalThis.__mobagenWorkerAgent = ok;
          return ok;
        }

        var worker = isWorkerAgent();
        if (worker && !globalThis.__mobagenForceMain) {
          // WORKER agent: blocking Atomics.wait is legal here.
          try {
            // Tripwire (see above): only a dropped agent guard can reach a
            // wait on a non-worker agent; the smoke asserts this stays 0.
            if (!worker) stats.main++;
            stats.blocking++;
            // Atomics.wait clamps negative timeouts to 0 (an instant return
            // = a hot loop); the contract's "<= 0 = forever" maps to
            // Infinity explicitly.
            Atomics.wait(HEAP32, index, expected, timeout_ms <= 0 ? Infinity : timeout_ms);
            return HEAP32[index] | 0;
          } catch (e) {
            // Capability failure (throw): fall through to bounded polling.
            stats.poll++;
          }
        } else {
          // MAIN agent (the browser host thread): NEVER call Atomics.wait —
          // bounded polling only. Also reached when the test hook forces the
          // main-thread classification.
          stats.poll++;
        }

        // Bounded-poll fallback. Paced: roughly one probe per millisecond —
        // never an unpaced busy loop. A finite timeout is a hard deadline;
        // the contract's infinite timeout keeps pacing until the word moves
        // (the GC release step — a worker — always flips the word; the
        // releaser never needs the polling agent's event loop, so this
        // terminates). Pacing primitive: Atomics.wait on a PRIVATE scratch
        // SharedArrayBuffer where that is legal (node, browser workers,
        // cross-origin-isolated Chrome main); on agents that forbid
        // Atomics.wait entirely (Firefox/Safari main threads) a Date-bounded
        // 1 ms CPU slice is the last resort — short, paced, rare.
        var deadline = timeout_ms > 0 ? Date.now() + timeout_ms : Infinity;
        for (;;) {
          var t0 = Date.now();
          var slept = false;
          try {
            var scratch = globalThis.__mobagenPollScratch;
            if (!scratch) {
              scratch = new Int32Array(new SharedArrayBuffer(4));
              globalThis.__mobagenPollScratch = scratch;
            }
            Atomics.wait(scratch, 0, 0, 1);
            slept = true;
          } catch (e) {
            slept = false;
          }
          if (!slept) {
            while (Date.now() - t0 < 1) {
              // paced CPU slice — see the block comment above
            }
          }
          v = HEAP32[index] | 0;
          if (v != expected) return v;
          if (Date.now() >= deadline) return v;
        }
      });

      // Monotonic ns: performance.now() (monotonic, sub-ms) when present,
      // Date.now() otherwise. Small magnitudes keep the double exact well
      // below nanosecond resolution.
      EM_JS(double, mobagen_web_shim_now_ns, (void), {
        var ms = typeof performance != "undefined" && performance && performance.now ? performance.now() : Date.now();
        return ms * 1e6;
      });

      EM_JS(int, mobagen_web_shim_stats, (int which), {
        var s = globalThis.__mobagenWebShimStats;
        if (!s) return 0;
        return which == 0 ? s.blocking : which == 1 ? s.main : s.poll;
      });

      EM_JS(void, mobagen_web_shim_set_force_main, (int forced), { globalThis.__mobagenForceMain = !!forced; });

    }  // namespace

    WebShim::WebShim() {
      region_ = static_cast<std::uint8_t*>(std::aligned_alloc(kBaseAlign, kInitialSize));
      region_size_ = region_ != nullptr ? kInitialSize : 0;
      vtable_.user_data = this;
      vtable_.region_acquire = [](void* ud, size_t minimum_bytes) -> MobagenRegionResult {
        return static_cast<WebShim*>(ud)->region_acquire(minimum_bytes);
      };
      vtable_.atomic_load = [](void* ud, MobagenAllocatorWord word) -> uint32_t { return static_cast<WebShim*>(ud)->atomic_load(word); };
      vtable_.atomic_store = [](void* ud, MobagenAllocatorWord word, uint32_t value) -> void {
        static_cast<WebShim*>(ud)->atomic_store(word, value);
      };
      vtable_.atomic_cas = [](void* ud, MobagenAllocatorWord word, uint32_t expected, uint32_t desired) -> int {
        return static_cast<WebShim*>(ud)->atomic_cas(word, expected, desired);
      };
      vtable_.atomic_fence = [](void* ud) -> void { static_cast<WebShim*>(ud)->atomic_fence(); };
      vtable_.word_wait = [](void* ud, MobagenAllocatorWord word, uint32_t current_value, int32_t timeout_ms) -> uint32_t {
        return static_cast<WebShim*>(ud)->word_wait(word, current_value, timeout_ms);
      };
      vtable_.word_notify_all = [](void* ud, MobagenAllocatorWord word) -> void { static_cast<WebShim*>(ud)->word_notify_all(word); };
      vtable_.now_ns = [](void* ud) -> uint64_t { return static_cast<WebShim*>(ud)->now_ns(); };
      vtable_.trace = [](void* ud, const char* message) -> void { static_cast<WebShim*>(ud)->trace(message); };
    }

    WebShim::~WebShim() {
      if (region_ != nullptr) std::free(region_);
    }

    Shim WebShim::shim() const noexcept { return Shim(vtable_); }

    const std::vector<std::string>& WebShim::trace_log() const noexcept { return trace_log_; }

    bool WebShim::shared_variant() noexcept {
#ifdef MOBAGEN_WEB_SHARED
      return true; /* shared variant: the emscripten heap is SAB-backed */
#else
      return false; /* isolated fallback (todo 6 probe picked a SAB-less context) */
#endif
    }

    std::uint32_t WebShim::stats_blocking_waits() noexcept { return static_cast<std::uint32_t>(mobagen_web_shim_stats(0)); }

    std::uint32_t WebShim::stats_main_thread_blocking_waits() noexcept { return static_cast<std::uint32_t>(mobagen_web_shim_stats(1)); }

    std::uint32_t WebShim::stats_poll_fallback_calls() noexcept { return static_cast<std::uint32_t>(mobagen_web_shim_stats(2)); }

    void WebShim::set_force_main_thread(bool forced) noexcept { mobagen_web_shim_set_force_main(forced ? 1 : 0); }

    std::uint32_t WebShim::word_index(MobagenAllocatorWord word) const noexcept {
      /* wasm32: the region pointer's integer value IS its linear-memory byte
       * offset; the Int32Array index of the word is (base + opaque) >> 2
       * (shim.hpp contract: opaque is the byte offset of a 4-aligned u32). */
      return static_cast<std::uint32_t>((reinterpret_cast<std::uintptr_t>(region_) + word.opaque) >> 2U);
    }

    MobagenRegionResult WebShim::region_acquire(std::size_t minimum_bytes) {
      MobagenRegionResult r{};
      if (region_size_ < minimum_bytes) {
        const std::size_t grown = round_up(minimum_bytes, kGrowAlign);
        std::uint8_t* next = static_cast<std::uint8_t*>(std::aligned_alloc(kBaseAlign, grown));
        if (next == nullptr) {
          std::snprintf(r.failure, sizeof(r.failure), "web shim: region alloc of %zu bytes failed", grown);
          r.ok = 0;
          trace(r.failure);
          return r;
        }
        /* No copy: region_acquire runs once per manager, BEFORE init carves
         * the region (same discipline as the native standalone shim). */
        std::free(region_);
        region_ = next;
        region_size_ = grown;
      }
      if (region_ == nullptr || region_size_ < minimum_bytes) {
        std::snprintf(r.failure, sizeof(r.failure), "web shim: region (%zu bytes) smaller than requested minimum %zu", region_size_, minimum_bytes);
        r.ok = 0;
        trace(r.failure);
        return r;
      }
      r.ok = 1;
      r.region.bytes = region_;
      r.region.size = region_size_;
      r.word = MobagenAllocatorWord{0}; /* control word at offset 0 */
      return r;
    }

    std::uint32_t WebShim::atomic_load(MobagenAllocatorWord word) noexcept {
#ifdef MOBAGEN_WEB_SHARED
      return static_cast<std::uint32_t>(mobagen_web_shim_atomics_load(word_index(word)));
#else
      /* Isolated: single-context region (plain ArrayBuffer heap) — plain u32
       * access has identical single-agent semantics and no SAB dependency
       * (Atomics.* REJECT non-shared buffers by throwing). */
      return *reinterpret_cast<volatile std::uint32_t*>(region_ + word.opaque);
#endif
    }

    void WebShim::atomic_store(MobagenAllocatorWord word, std::uint32_t value) noexcept {
#ifdef MOBAGEN_WEB_SHARED
      mobagen_web_shim_atomics_store(word_index(word), static_cast<int>(value));
#else
      *reinterpret_cast<volatile std::uint32_t*>(region_ + word.opaque) = value;
#endif
    }

    int WebShim::atomic_cas(MobagenAllocatorWord word, std::uint32_t expected, std::uint32_t desired) noexcept {
#ifdef MOBAGEN_WEB_SHARED
      return mobagen_web_shim_atomics_cas(word_index(word), static_cast<int>(expected), static_cast<int>(desired));
#else
      auto* p = reinterpret_cast<volatile std::uint32_t*>(region_ + word.opaque);
      if (*p == expected) {
        *p = desired;
        return 1;
      }
      return 0;
#endif
    }

    void WebShim::atomic_fence() noexcept {
#ifdef MOBAGEN_WEB_SHARED
      mobagen_web_shim_atomics_fence();
#else
      /* Single agent: a compiler barrier suffices (nothing to publish across
       * JS agents — the region is not shared in this variant). */
      __asm__ volatile("" : : : "memory");
#endif
    }

    std::uint32_t WebShim::word_wait(MobagenAllocatorWord word, std::uint32_t current_value, std::int32_t timeout_ms) noexcept {
#ifdef MOBAGEN_WEB_SHARED
      return static_cast<std::uint32_t>(mobagen_web_shim_wait(word_index(word), static_cast<int>(current_value), timeout_ms));
#else
      /*
       * ISOLATED DEGRADATION (loud, once): the variant picker (todo 6) chose
       * the isolated boot — single-context region, plain ArrayBuffer heap, no
       * SharedArrayBuffer. word_wait/word_notify_all degrade to no-op-yield
       * semantics: a single agent has NOTHING to block on. Concretely: the
       * GC epoch can only be odd while this same agent is inside collect(),
       * so a park predicate can never be observed true from alloc(); the
       * manager's wait sites are structurally unreachable here. The
       * ALGORITHM code is byte-identical across variants — only this shim
       * differs.
       */
      if (!isolated_wait_note_) {
        isolated_wait_note_ = true;
        trace("web shim: ISOLATED variant - no SharedArrayBuffer; region is single-context, atomics are plain u32 access, "
              "word_wait/word_notify_all degrade to no-op-yield (single agent: nothing to block on). "
              "Same algorithm code, degraded concurrency.");
      }
      return atomic_load(word);
#endif
    }

    void WebShim::word_notify_all(MobagenAllocatorWord word) noexcept {
#ifdef MOBAGEN_WEB_SHARED
      mobagen_web_shim_notify(word_index(word));
#else
      (void)word; /* no-op: no other agent is parked (see word_wait above) */
#endif
    }

    std::uint64_t WebShim::now_ns() noexcept { return static_cast<std::uint64_t>(mobagen_web_shim_now_ns()); }

    void WebShim::trace(const char* message) {
      std::lock_guard<std::mutex> lock(trace_mu_);
      trace_log_.emplace_back(message);
    }

    std::unique_ptr<WebShim> make_web_shim() { return std::make_unique<WebShim>(); }

  }  // namespace memory
}  // namespace mobagen
