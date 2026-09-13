#pragma once
// ============================================================================
// Scheduler — a work-stealing pool of worker threads running coroutine Tasks.
// ============================================================================
// Each worker owns a lock-free Chase-Lev deque (owner push/pop bottom; thieves
// steal top). Submissions from a NON-worker (e.g. main) go to a small global
// queue; reschedules from WITHIN a worker owner-push to that worker's own deque
// (Chase-Lev requires owner-only push/pop). Idle workers sleep on a condvar with
// a short timeout and are woken when work is pushed.
//
// A coroutine that `co_await`s a WaitGroup is parked off all deques (costs no
// worker) and rescheduled when the group completes — possibly on another thread.

#include "chase_lev_deque.hpp"
#include "task.hpp"
#include "wait_group.hpp"

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace jobs {

  // A coroutine that runs fn over [begin, end) once and finishes — the unit of
  // work for parallel_for.
  template <class Fn> inline Task make_range_task(Fn fn, std::size_t begin, std::size_t end) {
    fn(begin, end);
    co_return;
  }

  class Scheduler {
  public:
    // Threaded: a pool of worker threads (native std::thread / web Emscripten pthreads).
    // Inline: no threads — the caller DRIVES the queue in wait_idle(). The single-thread
    // fallback for environments without SharedArrayBuffer (some browser contexts).
    enum class Mode { Threaded, Inline };

#ifdef __EMSCRIPTEN__
    // Web default: Inline — no pthreads/SharedArrayBuffer requirement (GitHub
    // Pages and other static hosts cannot send COOP/COEP headers). Pass
    // Mode::Threaded explicitly on hosts that are cross-origin isolated.
    explicit Scheduler(unsigned workers = 0, Mode mode = Mode::Inline);
#else
    explicit Scheduler(unsigned workers = 0, Mode mode = Mode::Threaded);  // 0 => hardware_concurrency
#endif
    ~Scheduler();

    // Accepted work is owned by the scheduler and completed before shutdown
    // returns. A false result leaves the Task owned by the caller.
    bool kick(Task&& t, WaitGroup& completion);
    void wait_idle();  // threaded: block; inline: drive the queue here
    void shutdown();

    static int this_worker_id();  // -1 if not a worker thread
    static const Scheduler* this_scheduler();
    unsigned worker_count() const { return static_cast<unsigned>(workers_.size()); }
    bool accepting() const;
    std::size_t outstanding() const;

    // Data-parallel loop: split [0,n) into `grain`-sized chunks, kick each as a
    // job under `wg`. The caller then waits — `co_await wg` from a coroutine, or
    // `wait(wg)` from the driver thread.
    template <class Fn> bool parallel_for(std::size_t n, std::size_t grain, Fn fn, WaitGroup& wg) {
      if (grain == 0) grain = 1;
      for (std::size_t b = 0; b < n; b += grain) {
        const std::size_t e = (b + grain < n) ? b + grain : n;
        if (!kick(make_range_task(fn, b, e), wg)) return false;  // fn copied into each chunk
      }
      return true;
    }

    void wait(WaitGroup& wg);  // block (threaded) / drive (inline) until wg completes

  private:
    friend class WaitGroup;

    static constexpr std::size_t kDequeCap = 1u << 16;  // per-worker capacity
    // A 32-bit atomic stays native on the Web/WASM target as well as desktop.
    static constexpr std::uint32_t kAcceptingBit = std::uint32_t{1} << 31;
    static constexpr std::uint32_t kCountMask = ~kAcceptingBit;
    struct Worker {
      ChaseLevDeque<kDequeCap> q;
    };

    void worker_loop(int id);
    void drive();  // inline mode: run the queue on the calling thread
    void* try_steal(int id);
    void* pop_global();
    void run_one(std::coroutine_handle<> h);
    void schedule_accepted(std::coroutine_handle<> h);
    void wake_one();
    bool reserve_work(bool from_own_worker);
    void finish_work();

    bool inline_ = false;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::thread> threads_;
    // One atomic closes the submission-vs-shutdown race: the high bit is the
    // external admission gate and the remaining bits are accepted Tasks.
    std::atomic<std::uint32_t> work_state_{kAcceptingBit};

    std::mutex global_m_;  // injection queue (non-worker submits)
    std::deque<std::coroutine_handle<>> global_;

    std::mutex sleep_m_;
    std::condition_variable sleep_cv_;
    std::atomic<int> sleepers_{0};

    std::mutex shutdown_m_;
    bool stopped_ = false;  // guarded by shutdown_m_
  };

}  // namespace jobs
