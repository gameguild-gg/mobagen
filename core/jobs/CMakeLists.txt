# jobs module — coroutine work-stealing scheduler.
# Native: std::thread. Web: Emscripten pthreads (= Web Workers over SharedArrayBuffer).
# 5c: both targets from day one.  5d: jobs_c.h C ABI facade + parallel_for.

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

# add_jobs_exe(<name> <sources...>)
function(add_jobs_exe name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(EMSCRIPTEN)
        # -pthread => Web Workers. PROXY_TO_PTHREAD runs main() on a worker so it can
        # block (join/wait) without freezing the browser UI thread. Output as .html
        # uses Emscripten's default shell (a console pane). Serve with COOP/COEP
        # (tools/serve.py) so SharedArrayBuffer is available.
        target_compile_options(${name} PRIVATE -O3 -pthread)
        target_link_options(${name} PRIVATE
            -O3 -pthread
            -sPROXY_TO_PTHREAD
            -sPTHREAD_POOL_SIZE=16
            -sALLOW_MEMORY_GROWTH=1
            -sEXIT_RUNTIME=1)
        set_target_properties(${name} PROPERTIES SUFFIX ".html")
    else()
        target_link_libraries(${name} PRIVATE Threads::Threads)
    endif()
endfunction()

add_jobs_exe(jobs_demo   jobs_demo.cpp   scheduler.cpp)
add_jobs_exe(jobs_bench  jobs_bench.cpp  scheduler.cpp)
add_jobs_exe(jobs_c_demo jobs_c_demo.cpp jobs_c.cpp scheduler.cpp)  # uses only jobs_c.h
