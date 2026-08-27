// ============================================================================
// Prototype B — cooperative scheduler with STACKFUL fibers (Win32 Fiber API).
// ============================================================================
// Same demo as coro_scheduler.cpp (two tasks, round-robin, interleaved output),
// but each task runs on its OWN stack. A fiber switch swaps the stack pointer +
// registers, so a task can suspend from ANY call depth and resume exactly there.
//
// THE HEADLINE DIFFERENCE: `nested_step()` is an ordinary function, yet it can
// yield mid-way and be resumed inside itself. The stackless coroutine version
// cannot do that without turning the helper into a coroutine. This "suspend from
// anywhere, no rewrite" property is exactly why the Naughty Dog engine uses
// fibers (GDC 2015, "Parallelizing the Naughty Dog Engine Using Fibers").
//
// Win32 fibers are Windows-only and dependency-free — perfect for a clear demo.
// In the real engine the PORTABLE stackful path is boost.context (native) +
// emscripten_fiber (web, riding the Asyncify master already enables). The
// concept is identical; only the switch primitive changes.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <vector>

struct FiberTask {
  void* fiber = nullptr;
  bool done = false;
  const char* name = "";
};

static void* g_scheduler = nullptr;  // main thread, converted to a fiber
static std::vector<FiberTask> g_tasks;
static size_t g_running = 0;  // index of the task currently executing

// Hand control back to the scheduler. Callable from any depth — the whole stack
// is preserved across the switch.
static void yield_fiber() { SwitchToFiber(g_scheduler); }

static void finish_fiber() {
  g_tasks[g_running].done = true;
  SwitchToFiber(g_scheduler);  // never returns to the task
}

// An ordinary helper called BY the task. It yields from inside a nested call —
// the thing the stackless version structurally cannot do.
static void nested_step(const char* name) {
  std::printf("%s: step 2 (yielding from inside a nested helper)\n", name);
  yield_fiber();
  std::printf("%s: step 2 resumed (still inside that nested helper)\n", name);
}

static void __stdcall task_proc(void* param) {
  const char* name = static_cast<FiberTask*>(param)->name;
  std::printf("%s: step 1\n", name);
  yield_fiber();
  nested_step(name);  // <-- suspends from nested depth, resumes there
  std::printf("%s: step 3 (done)\n", name);
  finish_fiber();
}

int main() {
  std::printf("== Stackful fibers (Win32) ==\n");
  g_scheduler = ConvertThreadToFiber(nullptr);

  const char* names[] = {"A", "B"};
  g_tasks.resize(2);
  for (size_t i = 0; i < g_tasks.size(); ++i) {
    g_tasks[i].name = names[i];
    g_tasks[i].fiber = CreateFiber(64 * 1024, task_proc, &g_tasks[i]);
  }

  // Round-robin until every task has finished.
  bool any;
  do {
    any = false;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
      if (!g_tasks[i].done) {
        any = true;
        g_running = i;
        SwitchToFiber(g_tasks[i].fiber);  // runs until it yields or finishes
      }
    }
  } while (any);

  for (auto& t : g_tasks)
    if (t.fiber) DeleteFiber(t.fiber);
  std::printf("scheduler: all tasks complete\n");
  return 0;
}
