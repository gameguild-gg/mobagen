#pragma once
// ============================================================================
// WaitGroup — lock-free, SINGLE-WAITER job-dependency tracker.
// ============================================================================
// Our fork-join pattern has exactly one coroutine awaiting a given WaitGroup
// (a parent awaits its own children's group), so we don't need a mutex or a
// waiter list — just two atomics:
//   count_   : outstanding jobs (add raises it; each done() lowers it)
//   waiter_  : the single parked coroutine handle
// The park-vs-finish race is settled by an atomic exchange on `waiter_`: whoever
// swaps out the non-null handle is responsible for running/scheduling it — so it
// runs exactly once. No lock, no allocation. (Multiple awaiters => upgrade to a
// Treiber stack; not needed by our design.)

#include <atomic>
#include <cassert>
#include <coroutine>

namespace jobs {

  class Scheduler;

  class WaitGroup {
  public:
    WaitGroup() = default;
    WaitGroup(const WaitGroup&) = delete;
    WaitGroup& operator=(const WaitGroup&) = delete;

    void add(int n = 1) { count_.fetch_add(n, std::memory_order_relaxed); }
    void bind(Scheduler* s) { sched_ = s; }
    void done();  // scheduler.cpp (reschedules through the Scheduler)
    bool is_complete() const { return count_.load(std::memory_order_acquire) == 0; }

    struct Awaiter {
      WaitGroup& wg;
      bool await_ready() const noexcept { return wg.count_.load(std::memory_order_acquire) == 0; }
      bool await_suspend(std::coroutine_handle<> h) const {
        assert(wg.waiter_.load(std::memory_order_relaxed) == nullptr && "WaitGroup is single-waiter");
        wg.waiter_.store(h.address(), std::memory_order_release);
        if (wg.count_.load(std::memory_order_acquire) == 0) {
          // Finished while we were parking — arbitrate with done().
          void* p = wg.waiter_.exchange(nullptr, std::memory_order_acq_rel);
          // If we reclaimed the handle -> resume now.
          // Else done() took it and will schedule us -> stay suspended.
          if (p) return false;
        }
        return true;
      }
      void await_resume() const noexcept {}
    };
    Awaiter operator co_await() { return Awaiter{*this}; }

  private:
    std::atomic<int> count_{0};
    std::atomic<void*> waiter_{nullptr};
    Scheduler* sched_ = nullptr;
  };

}  // namespace jobs
