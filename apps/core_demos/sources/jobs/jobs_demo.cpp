// Demonstrates the coroutine work-stealing scheduler:
//   - a root job kicks 8 children and `co_await`s a WaitGroup,
//   - children run across multiple worker threads (work-stealing),
//   - the root resumes AFTER the group completes, possibly on a *different*
//     worker than it suspended on (cross-thread resume).
// Output ordering varies run to run — that's concurrency.
//
//   make core-examples
//   build/native/bin/Release/jobs_demo.exe

#include "scheduler.hpp"

#include <chrono>
#include <cstdio>

using namespace jobs;

// Spin briefly so scheduling is observable (don't sleep — keep the worker busy).
static void burn(int micros) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
  while (std::chrono::steady_clock::now() < end) { /* busy */
  }
}

static Task child(int i) {
  std::printf("    [worker %d] child %d\n", Scheduler::this_worker_id(), i);
  burn(2000);
  co_return;
}

static Task root(Scheduler& s) {
  std::printf("[worker %d] root: kick 8 children, then co_await\n", Scheduler::this_worker_id());
  WaitGroup wg;
  for (int i = 0; i < 8; ++i) s.kick(child(i), wg);
  co_await wg;  // suspend; this worker steals/runs the children meanwhile
  std::printf("[worker %d] root: resumed after all children (note the worker id)\n", Scheduler::this_worker_id());
  co_return;
}

int main() {
  Scheduler sched(4);
  std::printf("== coroutine work-stealing scheduler: %u workers ==\n", sched.worker_count());

  WaitGroup done;
  sched.kick(root(sched), done);
  sched.wait_idle();
  sched.shutdown();

  std::printf("done.\n");
  return 0;
}
