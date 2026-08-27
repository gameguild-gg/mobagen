#pragma once
// ============================================================================
// Task — a job, as a C++20 coroutine (our "Fiber", stackless).
// ============================================================================
// Perf: the coroutine FRAME is allocated from a per-thread POOL (free-list of
// fixed blocks) instead of malloc, via promise_type::operator new/delete. This
// removes the per-job heap allocation the benchmark flagged as overhead. Frames
// larger than the block size fall back to malloc (correct, just not pooled).

#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <vector>

namespace jobs {

  namespace detail {
    // Per-thread free-list of coroutine-frame blocks. A block freed on a different
    // thread than it was taken (cross-thread resume) simply joins that thread's pool
    // — correct, no leak (each pool frees its own list at thread exit).
    struct FramePool {
      static constexpr std::size_t kBlock = 512;  // covers our coroutine frames
      std::vector<void*> free_;

      void* alloc(std::size_t n) {
        if (n <= kBlock) {
          if (!free_.empty()) {
            void* p = free_.back();
            free_.pop_back();
            return p;
          }
          return std::malloc(kBlock);
        }
        return std::malloc(n);  // oversized: not pooled
      }
      void release(void* p, std::size_t n) {
        if (n <= kBlock)
          free_.push_back(p);  // keep for reuse
        else
          std::free(p);
      }
      ~FramePool() {
        for (void* p : free_) std::free(p);
      }
    };
    inline thread_local FramePool t_frame_pool;
  }  // namespace detail

  class WaitGroup;

  struct Task {
    struct promise_type {
      WaitGroup* completion = nullptr;  // signaled on finish (set by the scheduler)

      // Pool the coroutine frame instead of malloc/free.
      static void* operator new(std::size_t n) { return detail::t_frame_pool.alloc(n); }
      static void operator delete(void* p, std::size_t n) noexcept { detail::t_frame_pool.release(p, n); }

      Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
      std::suspend_always initial_suspend() noexcept { return {}; }
      std::suspend_always final_suspend() noexcept { return {}; }
      void return_void() noexcept {}
      void unhandled_exception() { std::terminate(); }
    };
    using handle_t = std::coroutine_handle<promise_type>;

    handle_t handle{};

    explicit Task(handle_t h) : handle(h) {}
    Task(Task&& o) noexcept : handle(o.handle) { o.handle = {}; }
    Task(const Task&) = delete;
    Task& operator=(Task&&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
      if (handle) handle.destroy();
    }

    handle_t release() {
      auto h = handle;
      handle = {};
      return h;
    }
  };

}  // namespace jobs
