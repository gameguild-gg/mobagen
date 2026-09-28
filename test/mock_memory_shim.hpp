#pragma once

/*
 * Mock memory shim (test-only): implements MobagenMemoryShim over a plain
 * heap buffer + std::atomic + std::condition_variable. Native-test twin of
 * what todo 13 (WAMR shared heap + pthread) and todo 14 (SAB + Atomics)
 * implement for production. Word contract: opaque == byte offset of a
 * 4-aligned u32 inside the region (see shim.hpp).
 */

#include "memory/shim.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

namespace mobagen {
  namespace memory {
    namespace testing {

      class MockShim {
      public:
        explicit MockShim(std::size_t bytes = 256U * 1024U) : region_(bytes, 0) {
          vtable_.user_data = this;
          vtable_.region_acquire = [](void* ud, size_t minimum) -> MobagenRegionResult {
            auto* self = static_cast<MockShim*>(ud);
            MobagenRegionResult r{};
            if (self->region_.size() < minimum) {
              r.ok = 0;
              std::snprintf(r.failure, sizeof(r.failure), "mock: region smaller than request");
              return r;
            }
            r.ok = 1;
            r.region.bytes = self->region_.data();
            r.region.size = self->region_.size();
            r.word = MobagenAllocatorWord{0};
            return r;
          };
          vtable_.atomic_load = [](void* ud, MobagenAllocatorWord w) -> uint32_t {
            auto* self = static_cast<MockShim*>(ud);
            std::lock_guard<std::mutex> lock(self->mu_);
            uint32_t v;
            std::memcpy(&v, self->region_.data() + w.opaque, sizeof(v));
            return v;
          };
          vtable_.atomic_store = [](void* ud, MobagenAllocatorWord w, uint32_t v) -> void {
            auto* self = static_cast<MockShim*>(ud);
            std::lock_guard<std::mutex> lock(self->mu_);
            std::memcpy(self->region_.data() + w.opaque, &v, sizeof(v));
            self->park_cv_.notify_all();
          };
          vtable_.atomic_cas = [](void* ud, MobagenAllocatorWord w, uint32_t expected, uint32_t desired) -> int {
            auto* self = static_cast<MockShim*>(ud);
            std::lock_guard<std::mutex> lock(self->mu_);
            uint32_t cur;
            std::memcpy(&cur, self->region_.data() + w.opaque, sizeof(cur));
            if (cur != expected) return 0;
            std::memcpy(self->region_.data() + w.opaque, &desired, sizeof(desired));
            self->park_cv_.notify_all();
            return 1;
          };
          vtable_.atomic_fence = [](void*) -> void {};
          vtable_.word_wait = [](void* ud, MobagenAllocatorWord w, uint32_t current, int32_t /*timeout_ms*/) -> uint32_t {
            auto* self = static_cast<MockShim*>(ud);
            std::unique_lock<std::mutex> lock(self->mu_);
            uint32_t v;
            std::memcpy(&v, self->region_.data() + w.opaque, sizeof(v));
            if (v != current) return v;
            self->park_cv_.wait(lock);
            std::memcpy(&v, self->region_.data() + w.opaque, sizeof(v));
            return v;
          };
          vtable_.word_notify_all = [](void* ud, MobagenAllocatorWord) -> void {
            auto* self = static_cast<MockShim*>(ud);
            std::lock_guard<std::mutex> lock(self->mu_);
            self->park_cv_.notify_all();
          };
          vtable_.now_ns = [](void* ud) -> uint64_t {
            auto* self = static_cast<MockShim*>(ud);
            return self->tick_.fetch_add(1) * 1000000ULL;
          };
          vtable_.trace = [](void* ud, const char* message) -> void {
            auto* self = static_cast<MockShim*>(ud);
            std::lock_guard<std::mutex> lock(self->mu_);
            self->trace_log_.emplace_back(message);
          };
        }

        [[nodiscard]] const MobagenMemoryShim& vtable() const noexcept { return vtable_; }
        [[nodiscard]] Shim shim() const noexcept { return Shim(vtable_); }
        [[nodiscard]] const std::vector<std::string>& trace_log() const noexcept { return trace_log_; }
        /* Steady fake clock for quiesce-timeout tests. */
        std::atomic<std::uint64_t> tick_{0};

      private:
        std::vector<std::uint8_t> region_;
        std::mutex mu_;
        std::condition_variable park_cv_;
        std::vector<std::string> trace_log_;
        MobagenMemoryShim vtable_{};
      };

    }  // namespace testing
  }    // namespace memory
}  // namespace mobagen
