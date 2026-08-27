#include "scheduler.hpp"

#include <chrono>

namespace jobs {

  namespace {
    thread_local int t_worker_id = -1;
  }

  int Scheduler::this_worker_id() { return t_worker_id; }

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
    if (!running_.exchange(false)) return;
    sleep_cv_.notify_all();
    for (auto& t : threads_)
      if (t.joinable()) t.join();
    threads_.clear();
  }

  void Scheduler::wake_one() {
    if (sleepers_.load(std::memory_order_relaxed) > 0) sleep_cv_.notify_one();
  }

  void Scheduler::schedule(std::coroutine_handle<> h) {
    const int w = t_worker_id;
    if (w >= 0) {
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

  void Scheduler::kick(Task&& t, WaitGroup& wg) {
    wg.bind(this);
    wg.add(1);
    auto h = t.release();
    h.promise().completion = &wg;
    outstanding_.fetch_add(1, std::memory_order_relaxed);
    schedule(h);
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
      outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    }
    // else: suspended (parked on a WaitGroup) — it'll be rescheduled.
  }

  void Scheduler::worker_loop(int id) {
    t_worker_id = id;
    while (running_.load(std::memory_order_relaxed)) {
      void* p = workers_[id]->q.pop();  // own bottom (LIFO)
      if (!p) p = try_steal(id);        // steal a victim's top (FIFO)
      if (!p) p = pop_global();         // injected work
      if (p) {
        run_one(std::coroutine_handle<>::from_address(p));
      } else {
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
    while (outstanding_.load(std::memory_order_acquire) > 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }

  // Wait for one specific WaitGroup (not all outstanding work). Driver-thread use:
  // threaded -> block-poll; inline -> drive the queue until it completes.
  void Scheduler::wait(WaitGroup& wg) {
    if (inline_) {
      t_worker_id = 0;
      while (!wg.is_complete()) {
        void* p = workers_[0]->q.pop();
        if (!p) p = pop_global();
        if (p) run_one(std::coroutine_handle<>::from_address(p));
      }
      t_worker_id = -1;
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
    t_worker_id = 0;
    while (outstanding_.load(std::memory_order_acquire) > 0) {
      void* p = workers_[0]->q.pop();
      if (!p) p = pop_global();
      if (p) run_one(std::coroutine_handle<>::from_address(p));
    }
    t_worker_id = -1;
  }

  // WaitGroup::done lives here because it reschedules through the Scheduler.
  // Lock-free single-waiter: claim the parked handle by atomic exchange.
  void WaitGroup::done() {
    if (count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {  // I was the last
      void* h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
      if (h) sched_->schedule(std::coroutine_handle<>::from_address(h));
    }
  }

}  // namespace jobs
