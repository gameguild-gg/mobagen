#include <doctest/doctest.h>
#include "jobs/scheduler.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace {

jobs::Task assign_result(int& result) {
  result = 42;
  co_return;
}

jobs::Task increment(std::atomic<int>& counter) {
  counter.fetch_add(1, std::memory_order_relaxed);
  co_return;
}

jobs::Task record_scheduler(jobs::Scheduler& expected, std::atomic<bool>& matched) {
  matched.store(jobs::Scheduler::this_scheduler() == &expected, std::memory_order_release);
  co_return;
}

jobs::Task submit_to(jobs::Scheduler& destination, std::atomic<bool>& accepted,
                     std::atomic<bool>& ran_on_destination) {
  jobs::WaitGroup inner_done;
  auto inner = record_scheduler(destination, ran_on_destination);
  accepted.store(destination.kick(std::move(inner), inner_done), std::memory_order_release);
  destination.wait(inner_done);
  co_return;
}

jobs::Task increment_after_delay(std::atomic<int>& completed) {
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  completed.fetch_add(1, std::memory_order_relaxed);
  co_return;
}

jobs::Task set_true(std::atomic<bool>& value) {
  value.store(true, std::memory_order_release);
  co_return;
}

jobs::Task submit_while_draining(jobs::Scheduler& scheduler, std::atomic<bool>& root_started,
                                 std::atomic<bool>& allow_children,
                                 std::atomic<bool>& child_accepted,
                                 std::atomic<bool>& child_ran) {
  root_started.store(true, std::memory_order_release);
  while (!allow_children.load(std::memory_order_acquire)) std::this_thread::yield();

  jobs::WaitGroup child_done;
  auto child = set_true(child_ran);
  const bool accepted = scheduler.kick(std::move(child), child_done);
  child_accepted.store(accepted, std::memory_order_release);
  if (accepted) co_await child_done;
  co_return;
}

jobs::Task set_true(bool& value) {
  value = true;
  co_return;
}

}  // namespace

TEST_CASE("Scheduler: kick single job, result available after wait") {
  jobs::Scheduler sched;
  int result = 0;
  jobs::WaitGroup wg;
  auto task = assign_result(result);
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
    auto task = increment(counter);
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

TEST_CASE("Scheduler: cross-scheduler submissions use the destination scheduler") {
  jobs::Scheduler source(1);
  jobs::Scheduler destination(1);
  jobs::WaitGroup outer_done;
  std::atomic<bool> accepted{false};
  std::atomic<bool> ran_on_destination{false};

  auto outer = submit_to(destination, accepted, ran_on_destination);

  REQUIRE(source.kick(std::move(outer), outer_done));
  source.wait(outer_done);
  CHECK(accepted.load(std::memory_order_acquire));
  CHECK(ran_on_destination.load(std::memory_order_acquire));
}

TEST_CASE("Scheduler: shutdown drains accepted jobs before joining workers") {
  constexpr int job_count = 32;
  std::atomic<int> completed{0};
  jobs::WaitGroup done;

  {
    jobs::Scheduler scheduler(1);
    for (int i = 0; i < job_count; ++i) {
      auto task = increment_after_delay(completed);
      REQUIRE(scheduler.kick(std::move(task), done));
    }
  }

  CHECK(completed.load(std::memory_order_relaxed) == job_count);
  CHECK(done.is_complete());
}

TEST_CASE("Scheduler: draining work may submit its required children") {
  jobs::Scheduler scheduler(1);
  jobs::WaitGroup root_done;
  std::atomic<bool> root_started{false};
  std::atomic<bool> allow_children{false};
  std::atomic<bool> child_accepted{false};
  std::atomic<bool> child_ran{false};

  auto root = submit_while_draining(scheduler, root_started, allow_children, child_accepted,
                                    child_ran);

  REQUIRE(scheduler.kick(std::move(root), root_done));
  while (!root_started.load(std::memory_order_acquire)) std::this_thread::yield();

  std::thread shutdown_thread([&] { scheduler.shutdown(); });
  while (scheduler.accepting()) std::this_thread::yield();
  allow_children.store(true, std::memory_order_release);
  shutdown_thread.join();

  CHECK(child_accepted.load(std::memory_order_acquire));
  CHECK(child_ran.load(std::memory_order_acquire));
  CHECK(root_done.is_complete());
  CHECK(scheduler.outstanding() == 0);
}

TEST_CASE("Scheduler: shutdown is idempotent and rejects later submissions") {
  jobs::Scheduler scheduler(1);
  scheduler.shutdown();
  scheduler.shutdown();

  jobs::WaitGroup done;
  bool ran = false;
  auto rejected = set_true(ran);

  CHECK_FALSE(scheduler.kick(std::move(rejected), done));
  CHECK_FALSE(ran);
  CHECK(done.is_complete());
  CHECK(scheduler.outstanding() == 0);
}

TEST_CASE("WaitGroup: scheduler binding and completion count reject misuse") {
  jobs::Scheduler first(1, jobs::Scheduler::Mode::Inline);
  jobs::Scheduler second(1, jobs::Scheduler::Mode::Inline);
  jobs::WaitGroup group;

  CHECK(group.bind(&first));
  CHECK(group.bind(&first));
  CHECK_FALSE(group.bind(&second));
  CHECK_FALSE(group.done());
  CHECK(group.is_complete());
}
