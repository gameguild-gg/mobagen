#ifndef MOBAGEN_MEMORY_NATIVE_SHIM_H
#define MOBAGEN_MEMORY_NATIVE_SHIM_H

/*
 * Native memory shim (todo 13, micro-task A) — MobagenMemoryShim vtable
 * over a native process region + GCC/Clang __atomic builtins + pthread
 * mutex/condvar. Native-only (NOT built under EMSCRIPTEN; the web shim is
 * todo 14 over SAB + Atomics).
 *
 * Two ownership modes:
 *  - standalone: owns a 16-byte-aligned region (std::aligned_alloc,
 *    initially 256 KiB), grown on demand in region_acquire.
 *  - foreign: borrows a region it never frees (the WAMR shared heap).
 *
 * T26 call site (todo 13 integration):
 *   auto r = backend.shared_heap_region();
 *   auto s = make_native_shim(r.data, r.size);
 *   MemoryManager m(s->shim());
 */

#include "memory/shim.hpp"

#include <pthread.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mobagen {
  namespace memory {

    class NativeShim {
    public:
      /* Standalone: owns a 16-byte-aligned region, initially 256 KiB. */
      NativeShim();
      /* Foreign: borrows a region it never frees (e.g. WAMR shared heap). */
      NativeShim(void* bytes, std::size_t size);
      ~NativeShim();

      NativeShim(const NativeShim&) = delete;
      NativeShim& operator=(const NativeShim&) = delete;

      /* vtable with this as user_data (see shim.hpp for the contract). */
      [[nodiscard]] Shim shim() const noexcept;

      /* Best-effort trace sink log (loud failures). */
      [[nodiscard]] const std::vector<std::string>& trace_log() const noexcept;

      /* Exposed for tests / direct vtable wiring. */
      [[nodiscard]] const MobagenMemoryShim& vtable() const noexcept { return vtable_; }

    private:
      MobagenRegionResult region_acquire(std::size_t minimum_bytes);
      std::uint32_t atomic_load(MobagenAllocatorWord word) noexcept;
      void atomic_store(MobagenAllocatorWord word, std::uint32_t value) noexcept;
      int atomic_cas(MobagenAllocatorWord word, std::uint32_t expected, std::uint32_t desired) noexcept;
      void atomic_fence() noexcept;
      std::uint32_t word_wait(MobagenAllocatorWord word, std::uint32_t current_value, std::int32_t timeout_ms);
      void word_notify_all(MobagenAllocatorWord word);
      static std::uint64_t now_ns() noexcept;
      void trace(const char* message);

      struct Bucket {
        pthread_mutex_t mu;
        pthread_cond_t cv;
      };

      std::uint8_t* region_{nullptr};
      std::size_t region_size_{0};
      bool owned_{false};
      Bucket buckets_[16];
      std::mutex trace_mu_;
      std::vector<std::string> trace_log_;
      MobagenMemoryShim vtable_{};
    };

    /* Free factories: standalone / foreign (WAMR shared heap). */
    [[nodiscard]] std::unique_ptr<NativeShim> make_native_shim();
    [[nodiscard]] std::unique_ptr<NativeShim> make_native_shim(void* bytes, std::size_t size);

  }  // namespace memory
}  // namespace mobagen

#endif /* MOBAGEN_MEMORY_NATIVE_SHIM_H */
