#include "memory/native_shim.hpp"

#include <pthread.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>

namespace mobagen {
  namespace memory {

    namespace {
      constexpr std::size_t kBaseAlign = 16;             /* region base alignment (shim.hpp contract) */
      constexpr std::size_t kInitialSize = 256U * 1024U; /* standalone initial region */
      constexpr std::size_t kGrowAlign = 64U * 1024U;    /* growth quantum */

      std::size_t round_up(std::size_t value, std::size_t quantum) noexcept {
        return (value + quantum - 1) / quantum * quantum;
      }
    }  // namespace

    NativeShim::NativeShim() : owned_(true) {
      region_ = static_cast<std::uint8_t*>(std::aligned_alloc(kBaseAlign, kInitialSize));
      region_size_ = region_ != nullptr ? kInitialSize : 0;
      for (auto& bucket : buckets_) {
        pthread_mutex_init(&bucket.mu, nullptr);
        pthread_cond_init(&bucket.cv, nullptr); /* default condattr = CLOCK_REALTIME */
      }
      vtable_.user_data = this;
      vtable_.region_acquire = [](void* ud, size_t minimum_bytes) -> MobagenRegionResult {
        return static_cast<NativeShim*>(ud)->region_acquire(minimum_bytes);
      };
      vtable_.atomic_load = [](void* ud, MobagenAllocatorWord word) -> uint32_t {
        return static_cast<NativeShim*>(ud)->atomic_load(word);
      };
      vtable_.atomic_store = [](void* ud, MobagenAllocatorWord word, uint32_t value) -> void {
        static_cast<NativeShim*>(ud)->atomic_store(word, value);
      };
      vtable_.atomic_cas = [](void* ud, MobagenAllocatorWord word, uint32_t expected, uint32_t desired) -> int {
        return static_cast<NativeShim*>(ud)->atomic_cas(word, expected, desired);
      };
      vtable_.atomic_fence = [](void* ud) -> void { static_cast<NativeShim*>(ud)->atomic_fence(); };
      vtable_.word_wait = [](void* ud, MobagenAllocatorWord word, uint32_t current_value, int32_t timeout_ms) -> uint32_t {
        return static_cast<NativeShim*>(ud)->word_wait(word, current_value, timeout_ms);
      };
      vtable_.word_notify_all = [](void* ud, MobagenAllocatorWord word) -> void {
        static_cast<NativeShim*>(ud)->word_notify_all(word);
      };
      vtable_.now_ns = [](void*) -> uint64_t { return NativeShim::now_ns(); };
      vtable_.trace = [](void* ud, const char* message) -> void { static_cast<NativeShim*>(ud)->trace(message); };
    }

    NativeShim::NativeShim(void* bytes, std::size_t size) : NativeShim() {
      /* Foreign mode: borrow, never free, never grow. */
      region_ = static_cast<std::uint8_t*>(bytes);
      region_size_ = size;
      owned_ = false;
    }

    NativeShim::~NativeShim() {
      for (auto& bucket : buckets_) {
        pthread_mutex_destroy(&bucket.mu);
        pthread_cond_destroy(&bucket.cv);
      }
      if (owned_ && region_ != nullptr) std::free(region_);
    }

    Shim NativeShim::shim() const noexcept { return Shim(vtable_); }

    const std::vector<std::string>& NativeShim::trace_log() const noexcept { return trace_log_; }

    MobagenRegionResult NativeShim::region_acquire(std::size_t minimum_bytes) {
      MobagenRegionResult r{};
      if (owned_ && region_size_ < minimum_bytes) {
        const std::size_t grown = round_up(minimum_bytes, kGrowAlign);
        std::uint8_t* next = static_cast<std::uint8_t*>(std::aligned_alloc(kBaseAlign, grown));
        if (next == nullptr) {
          std::snprintf(r.failure, sizeof(r.failure), "native shim: region alloc of %zu bytes failed", grown);
          r.ok = 0;
          trace(r.failure);
          return r;
        }
        std::free(region_);
        region_ = next;
        region_size_ = grown;
      }
      if (region_ == nullptr || region_size_ < minimum_bytes) {
        std::snprintf(r.failure, sizeof(r.failure),
                      "native shim: foreign region (%zu bytes) smaller than requested minimum %zu", region_size_,
                      minimum_bytes);
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

    std::uint32_t NativeShim::atomic_load(MobagenAllocatorWord word) noexcept {
      auto* p = reinterpret_cast<std::uint32_t*>(region_ + word.opaque);
      return __atomic_load_n(p, __ATOMIC_SEQ_CST);
    }

    void NativeShim::atomic_store(MobagenAllocatorWord word, std::uint32_t value) noexcept {
      auto* p = reinterpret_cast<std::uint32_t*>(region_ + word.opaque);
      __atomic_store_n(p, value, __ATOMIC_SEQ_CST);
    }

    int NativeShim::atomic_cas(MobagenAllocatorWord word, std::uint32_t expected, std::uint32_t desired) noexcept {
      auto* p = reinterpret_cast<std::uint32_t*>(region_ + word.opaque);
      return __atomic_compare_exchange_n(p, &expected, desired, /*weak=*/false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
                 ? 1
                 : 0;
    }

    void NativeShim::atomic_fence() noexcept { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

    std::uint32_t NativeShim::word_wait(MobagenAllocatorWord word, std::uint32_t current_value, std::int32_t timeout_ms) {
      Bucket& bucket = buckets_[(word.opaque >> 2) & 15];
      auto* p = reinterpret_cast<std::uint32_t*>(region_ + word.opaque);
      pthread_mutex_lock(&bucket.mu);
      std::uint32_t v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
      if (v != current_value) {
        pthread_mutex_unlock(&bucket.mu);
        return v;
      }
      if (timeout_ms < 0) {
        do {
          pthread_cond_wait(&bucket.cv, &bucket.mu);
          v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
        } while (v == current_value);
      } else {
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += static_cast<time_t>(timeout_ms / 1000);
        ts.tv_nsec += static_cast<long>(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
          ts.tv_sec += 1;
          ts.tv_nsec -= 1000000000L;
        }
        int rc = 0;
        while ((rc = pthread_cond_timedwait(&bucket.cv, &bucket.mu, &ts)) == 0) {
          v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
          if (v != current_value) break;
        }
        /* ETIMEDOUT (or any stop): return the observed value. */
        v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
      }
      pthread_mutex_unlock(&bucket.mu);
      return v;
    }

    void NativeShim::word_notify_all(MobagenAllocatorWord word) {
      Bucket& bucket = buckets_[(word.opaque >> 2) & 15];
      pthread_mutex_lock(&bucket.mu);
      pthread_cond_broadcast(&bucket.cv);
      pthread_mutex_unlock(&bucket.mu);
    }

    std::uint64_t NativeShim::now_ns() noexcept {
      timespec ts{};
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(ts.tv_nsec);
    }

    void NativeShim::trace(const char* message) {
      std::lock_guard<std::mutex> lock(trace_mu_);
      trace_log_.emplace_back(message);
    }

    std::unique_ptr<NativeShim> make_native_shim() { return std::make_unique<NativeShim>(); }

    std::unique_ptr<NativeShim> make_native_shim(void* bytes, std::size_t size) {
      return std::make_unique<NativeShim>(bytes, size);
    }

  }  // namespace memory
}  // namespace mobagen
