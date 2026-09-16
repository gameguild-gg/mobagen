#include "scheduler.hpp"

#include <chrono>

namespace jobs {

  namespace {
    thread_local Scheduler* t_scheduler = nullptr;
    thread_local int t_worker_id = -1;

    class WorkerBinding {
    public:
      WorkerBinding(Scheduler* scheduler, int worker_id) : previous_scheduler_(t_scheduler), previous_worker_id_(t_worker_id) {
        t_scheduler = scheduler;
        t_worker_id = worker_id;
      }
      ~WorkerBinding() {
        t_scheduler = previous_scheduler_;
        t_worker_id = previous_worker_id_;
      }

    private:
      Scheduler* previous_scheduler_;
      int previous_worker_id_;
    };
  }  // namespace

  int Scheduler::this_worker_id() { return t_worker_id; }
  const Scheduler* Scheduler::this_scheduler() { return t_scheduler; }

  bool Scheduler::accepting() const { return (work_state_.load(std::memory_order_acquire) & kAcceptingBit) != 0; }

  std::size_t Scheduler::outstanding() const { return static_cast<std::size_t>(work_state_.load(std::memory_order_acquire) & kCountMask); }

  Scheduler::Scheduler(unsigned workers, Mode mode) : inline_(mode == Mode::Inline) {
    unsigned n = inline_ ? 1u : (workers ? workers : std::thread::hardware_concurrency());
    if (n == 0) n = 4;
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) workers_.push_back(std::make_unique<Worker>());
    if (!inline_) {
      threads_.reserve(n);
      for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this, i] { worker_loop(static_cast<int>(i)); });
    }
  }

  Scheduler::~Scheduler() { shutdown(); }

  void Scheduler::shutdown() {
    std::unique_lock<std::mutex> shutdown_lock(shutdown_m_);
    if (stopped_) return;

    // Atomically close external admission while retaining the exact accepted
    // work count. Tasks already running may still submit their own children.
    work_state_.fetch_and(kCountMask, std::memory_order_acq_rel);
    sleep_cv_.notify_all();
    if (inline_) {
      drive();
    } else {
      for (auto& t : threads_)
        if (t.joinable()) t.join();
    }
    threads_.clear();
    stopped_ = true;
  }

  void Scheduler::wake_one() {
    if (sleepers_.load(std::memory_order_relaxed) > 0) sleep_cv_.notify_one();
  }

  void Scheduler::schedule_accepted(std::coroutine_handle<> h) {
    const int w = t_worker_id;
    if (t_scheduler == this && w >= 0 && static_cast<std::size_t>(w) < workers_.size()) {
      if (!workers_[w]->q.push(h.address())) {  // owner push; overflow -> global
        std::lock_guard<std::mutex> lk(global_m_);
        global_.push_back(h);
      }
    } else {
      std::lock_guard<std::mutex> lk(global_m_);  // non-worker submit
      global_.push_back(h);
    }
    wake_one();
  }

  bool Scheduler::reserve_work(bool from_own_worker) {
    std::uint32_t state = work_state_.load(std::memory_order_acquire);
    for (;;) {
      const std::uint32_t count = state & kCountMask;
      const bool may_submit = (state & kAcceptingBit) != 0 || (from_own_worker && count != 0);
      if (!may_submit || count == kCountMask) return false;
      if (work_state_.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return true;
      }
    }
  }

  void Scheduler::finish_work() {
    const std::uint32_t previous = work_state_.fetch_sub(1, std::memory_order_acq_rel);
    assert((previous & kCountMask) != 0 && "Scheduler work count underflow");
    if ((previous & kCountMask) == 1) sleep_cv_.notify_all();
  }

  bool Scheduler::kick(Task&& t, WaitGroup& wg) {
    if (!t.handle || !reserve_work(t_scheduler == this)) return false;
    if (!wg.bind(this) || !wg.add(1)) {
      finish_work();
      return false;
    }
    auto h = t.release();
    h.promise().completion = &wg;
    schedule_accepted(h);
    return true;
  }

  void* Scheduler::try_steal(int id) {
    const unsigned n = static_cast<unsigned>(workers_.size());
    for (unsigned k = 1; k < n; ++k)
      if (void* p = workers_[(static_cast<unsigned>(id) + k) % n]->q.steal()) return p;
    return nullptr;
  }

  void* Scheduler::pop_global() {
    std::lock_guard<std::mutex> lk(global_m_);
    if (global_.empty()) return nullptr;
    auto h = global_.front();
    global_.pop_front();
    return h.address();
  }

  void Scheduler::run_one(std::coroutine_handle<> h) {
    h.resume();
    if (h.done()) {
      auto th = std::coroutine_handle<Task::promise_type>::from_address(h.address());
      WaitGroup* c = th.promise().completion;
      th.destroy();
      if (c) c->done();
      finish_work();
    }
    // else: suspended (parked on a WaitGroup) — it'll be rescheduled.
  }

  void Scheduler::worker_loop(int id) {
    WorkerBinding binding(this, id);
    for (;;) {
      void* p = workers_[id]->q.pop();  // own bottom (LIFO)
      if (!p) p = try_steal(id);        // steal a victim's top (FIFO)
      if (!p) p = pop_global();         // injected work
      if (p) {
        run_one(std::coroutine_handle<>::from_address(p));
      } else {
        const std::uint32_t state = work_state_.load(std::memory_order_acquire);
        if ((state & kAcceptingBit) == 0 && (state & kCountMask) == 0) break;
        std::unique_lock<std::mutex> lk(sleep_m_);
        sleepers_.fetch_add(1, std::memory_order_relaxed);
        sleep_cv_.wait_for(lk, std::chrono::microseconds(500));  // timeout = lost-wake backstop
        sleepers_.fetch_sub(1, std::memory_order_relaxed);
      }
    }
  }

  void Scheduler::wait_idle() {
    if (inline_) {
      drive();
      return;
    }
    while (outstanding() > 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }

  // Wait for one specific WaitGroup (not all outstanding work). Driver-thread use:
  // threaded -> block-poll; inline -> drive the queue until it completes.
  void Scheduler::wait(WaitGroup& wg) {
    if (inline_) {
      WorkerBinding binding(this, 0);
      while (!wg.is_complete()) {
        void* p = workers_[0]->q.pop();
        if (!p) p = pop_global();
        if (p) run_one(std::coroutine_handle<>::from_address(p));
      }
    } else {
      while (!wg.is_complete()) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }

  // Inline (fallback) mode: the calling thread IS the worker. It drains its own
  // deque + the global queue until nothing is outstanding. Assumes self-contained
  // cooperative work (no waits on anything external) — which holds for our jobs:
  // a parent always enqueues its children before it parks, so there is always
  // ready work until the graph completes.
  void Scheduler::drive() {
    WorkerBinding binding(this, 0);
    while (outstanding() > 0) {
      void* p = workers_[0]->q.pop();
      if (!p) p = pop_global();
      if (p)
        run_one(std::coroutine_handle<>::from_address(p));
      else
        std::this_thread::yield();
    }
  }

  // WaitGroup::done lives here because it reschedules through the Scheduler.
  // Lock-free single-waiter: claim the parked handle by atomic exchange.
  bool WaitGroup::done() {
    std::uint32_t count = count_.load(std::memory_order_acquire);
    do {
      if (count == 0) return false;
    } while (!count_.compare_exchange_weak(count, count - 1, std::memory_order_acq_rel, std::memory_order_acquire));

    if (count == 1) {  // I was the last
      void* h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
      if (h) {
        auto handle = std::coroutine_handle<>::from_address(h);
        if (Scheduler* scheduler = sched_.load(std::memory_order_acquire))
          scheduler->schedule_accepted(handle);
        else
          handle.resume();
      }
    }
    return true;
  }

}  // namespace jobs
