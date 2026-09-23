# Memory manager + GC core (todo 12) — platform-neutral.
#
# Algorithm sources compile everywhere (native + emscripten): they contain
# zero platform includes — every primitive (region, atomics, wait/notify,
# clock) reaches them through the MobagenMemoryShim vtable (shim.hpp).
# Platform implementations are todo 13 (WAMR shared heap + pthread) and
# todo 14 (SAB + Atomics); tests here drive the core through a mock shim.
add_library(mobagen_memory_core STATIC memory_manager.cpp memory_manager.hpp object_header.h shim.hpp)
add_library(mobagen::memory_core ALIAS mobagen_memory_core)
target_include_directories(mobagen_memory_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_memory_core PUBLIC c_std_11 cxx_std_20)

# Todo 13 (micro-task A): native memory shim — MobagenMemoryShim over a native
# process region + __atomic builtins + pthread mutex/condvar. The web shim
# (todo 14, SAB + Atomics) lives elsewhere; native only.
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  add_library(mobagen_memory_native_shim STATIC native_shim.cpp native_shim.hpp)
  add_library(mobagen::memory_native_shim ALIAS mobagen_memory_native_shim)
  target_include_directories(mobagen_memory_native_shim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(mobagen_memory_native_shim PUBLIC cxx_std_20)
  target_link_libraries(mobagen_memory_native_shim PUBLIC Threads::Threads)
endif()
