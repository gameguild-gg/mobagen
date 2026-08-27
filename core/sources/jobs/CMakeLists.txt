# jobs module — coroutine work-stealing scheduler.
#
# Native: std::thread. Web: single-threaded inline scheduling (Mode::Inline).
#
# This directory builds the reusable module only. Demo/bench executables live in
# apps/core_demos/sources/jobs and link mobagen::core_jobs.

if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
endif()

add_library(mobagen_core_jobs STATIC scheduler.cpp jobs_c.cpp)
add_library(mobagen::core_jobs ALIAS mobagen_core_jobs)
target_include_directories(mobagen_core_jobs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_jobs PUBLIC cxx_std_20)

if(EMSCRIPTEN)
  # Web builds are single-threaded (no SharedArrayBuffer / COOP-COEP requirement on static hosts)
  # and the Scheduler defaults to Mode::Inline there.
  target_link_options(mobagen_core_jobs INTERFACE -sALLOW_MEMORY_GROWTH=1)
else()
  target_link_libraries(mobagen_core_jobs PUBLIC Threads::Threads)
endif()
