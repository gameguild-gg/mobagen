#include <doctest/doctest.h>
#include "jobs/scheduler.hpp"
#include <atomic>
#include <thread>

TEST_CASE("Scheduler: kick single job, result available after wait") {
  jobs::Scheduler sched;
  int result = 0;
  jobs::WaitGroup wg;
  auto task = [&]() -> jobs::Task {
    result = 42;
    co_return;
  }();
  sched.kick(std::move(task), wg);
  sched.wait(wg);
  CHECK(result == 42);
}

TEST_CASE("WaitGroup: N dependencies all complete before continuation") {
  jobs::Scheduler sched;
  std::atomic<int> counter{0};
  const int N = 8;
  jobs::WaitGroup wg;
  for (int i = 0; i < N; ++i) {
    auto task = [&]() -> jobs::Task {
      counter.fetch_add(1, std::memory_order_relaxed);
      co_return;
    }();
    sched.kick(std::move(task), wg);
  }
  sched.wait(wg);
  CHECK(counter.load() == N);
}

TEST_CASE("Chase-Lev deque: push N, steal all from other thread") {
  jobs::ChaseLevDeque<1024> deque;
  const int N = 32;
  for (int i = 0; i < N; ++i) {
    deque.push(reinterpret_cast<void*>(static_cast<std::uintptr_t>(i + 1)));
  }
  std::atomic<int> stolen_count{0};
  std::thread thief([&]() {
    for (int i = 0; i < 128; ++i) {
      auto* p = deque.steal();
      if (p) {
        stolen_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });
  thief.join();
  int owner_remaining = 0;
  while (true) {
    auto* p = deque.pop();
    if (!p) break;
    ++owner_remaining;
  }
  CHECK(stolen_count.load() + owner_remaining == N);
}
