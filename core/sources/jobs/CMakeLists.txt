# jobs module — coroutine work-stealing scheduler.
#
# Native: std::thread.
# Web: Emscripten pthreads (= Web Workers over SharedArrayBuffer).
#
# This directory builds the reusable module only. Demo/bench executables live in
# apps/core_demos/sources/jobs and link mobagen::core_jobs.

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

add_library(mobagen_core_jobs STATIC
    scheduler.cpp
    jobs_c.cpp
)
add_library(mobagen::core_jobs ALIAS mobagen_core_jobs)
target_include_directories(mobagen_core_jobs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_jobs PUBLIC cxx_std_20)

if(EMSCRIPTEN)
    # Public because any executable using the threaded job system must be linked
    # as a pthread-enabled wasm module.
    target_compile_options(mobagen_core_jobs PUBLIC -pthread)
    target_link_options(mobagen_core_jobs INTERFACE
        -pthread
        -sPTHREAD_POOL_SIZE=16
        -sALLOW_MEMORY_GROWTH=1)
else()
    target_link_libraries(mobagen_core_jobs PUBLIC Threads::Threads)
endif()
