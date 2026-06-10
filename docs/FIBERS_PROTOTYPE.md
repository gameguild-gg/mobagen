# Fiber Prototypes

Two cooperative schedulers running the **same** demo (two tasks, A and B, each
doing three steps, round-robined so output interleaves) with **different
suspension machinery**. The point is to *feel* the stackless vs stackful split
before we choose the engine's primitive.

| File | Model | Suspends from | Stack | Portable real-engine path |
|------|-------|---------------|-------|---------------------------|
| `coro_scheduler.cpp`     | C++20 coroutine (stackless) | only `co_await` in the coroutine itself | heap frame | C++20 coroutines (compile to wasm directly) |
| `stackful_scheduler.cpp` | Win32 fiber (stackful)      | **any call depth**                      | own stack | boost.context (native) + `emscripten_fiber` (web, Asyncify) |

The stackful demo is rigged to show the headline difference: `nested_step()` is
an *ordinary* function that yields from inside itself and resumes there. The
coroutine version cannot do that without making the helper a coroutine too.

## Build & run (native, Windows)

```powershell
cmake -B build/native -DCMAKE_BUILD_TYPE=Release -DBUILD_PROTOTYPES=ON
cmake --build build/native --config Release --target coro_demo
cmake --build build/native --config Release --target fiber_demo
build\native\bin\Release\coro_demo.exe
build\native\bin\Release\fiber_demo.exe
```

## Why this matters for the engine

The Naughty Dog talk (GDC 2015, *Parallelizing the Naughty Dog Engine Using
Fibers*) uses **stackful fibers** precisely so existing deep call trees can
`WaitForCounter()` from anywhere without being rewritten as coroutines. The
trade-off is memory: each fiber owns a stack (e.g. 64 KB), so a pool of N fibers
costs N stacks. Coroutines are cheaper per-instance but viral (`co_await` spreads
up the call chain) and can't suspend across a normal call.
