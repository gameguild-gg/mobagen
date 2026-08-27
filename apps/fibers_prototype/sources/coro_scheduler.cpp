// ============================================================================
// Prototype A — cooperative scheduler with C++20 STACKLESS coroutines.
// ============================================================================
// Two tasks (A, B) each do 3 steps, yielding between them. A single-threaded
// scheduler round-robins them, so output interleaves. This is the *stackless*
// model: a coroutine is a compiler-built state machine; only its locals live in
// a heap frame, and it can suspend ONLY at `co_await` points inside itself.
//
// KEY LIMITATION (contrast with the stackful version): a plain helper function
// called by the task cannot yield — to suspend, the helper would itself have to
// become a coroutine and be co_await-ed. Suspension cannot cross a normal call.

#include <coroutine>
#include <cstdio>
#include <queue>

// ---- A minimal coroutine "task" type -------------------------------------
// initial_suspend = suspend_always so creating the task does NOT start it; the
// scheduler starts it by resuming the handle. final_suspend = suspend_always so
// the frame survives after completion until we destroy it.
struct Task {
  struct promise_type {
    Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };
  std::coroutine_handle<promise_type> handle;
  explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
  Task(Task&& o) noexcept : handle(o.handle) { o.handle = nullptr; }
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() {
    if (handle) handle.destroy();
  }
};

// ---- The scheduler's run queue of ready coroutines -----------------------
static std::queue<std::coroutine_handle<>> g_ready;

// Awaitable: always suspend, and re-queue the current coroutine so the
// scheduler resumes it on the next round.
struct Yield {
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) const noexcept { g_ready.push(h); }
  void await_resume() const noexcept {}
};
static Yield yield() { return {}; }

// A normal function CANNOT yield here — it isn't a coroutine. The work has to
// live directly in the coroutine body (or in another coroutine that's awaited).
static Task make_task(const char* name) {
  std::printf("%s: step 1\n", name);
  co_await yield();
  std::printf("%s: step 2\n", name);
  co_await yield();
  std::printf("%s: step 3 (done)\n", name);
}

int main() {
  std::printf("== C++20 coroutines (stackless) ==\n");
  Task a = make_task("A");
  Task b = make_task("B");
  g_ready.push(a.handle);
  g_ready.push(b.handle);

  // Round-robin until everyone is finished. A task that hits co_await re-queues
  // itself; a task that returns simply isn't re-queued, so the queue drains.
  while (!g_ready.empty()) {
    auto h = g_ready.front();
    g_ready.pop();
    h.resume();  // runs until the next co_await or completion
  }
  std::printf("scheduler: all tasks complete\n");
  return 0;
}
