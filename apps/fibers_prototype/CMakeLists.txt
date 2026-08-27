# Fiber prototypes — two cooperative schedulers, same demo, different machinery. Native only. Built
# when -DBUILD_PROTOTYPES=ON.

# A — C++20 stackless coroutines. Needs C++20 (coroutines), forced per-target.
add_executable(coro_demo sources/coro_scheduler.cpp)
set_target_properties(coro_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# B — stackful fibers via the Win32 Fiber API (Windows only).
if(WIN32)
  add_executable(fiber_demo sources/stackful_scheduler.cpp)
  set_target_properties(fiber_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif()
