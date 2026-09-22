# BrowserWasmBackend (dynamic-loading-all-platforms todo 8).
#
# Compiled ONLY under EMSCRIPTEN (both web variants from todo 5): the backend
# delegates plugin compilation/instantiation to the browser's own wasm engine
# through the synchronous WebAssembly.Module/Instance constructors. The whole
# directory is added from ../CMakeLists.txt inside `if(EMSCRIPTEN)`.

# ---------------------------------------------------------------------------
# Reference guest: standalone wasm compiled from test_guest.c by the same emcc
# that builds the engine. One wasm page of linear memory, exported as
# 'memory'; full plugin contract exports; mobagen_smoke_add as the annotated
# (module_abi.h) representative export.
# ---------------------------------------------------------------------------
set(MOBAGEN_BROWSER_GUEST_WASM "${CMAKE_CURRENT_BINARY_DIR}/mobagen_smoke_guest.wasm")
set(MOBAGEN_BROWSER_GUEST_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MOBAGEN_BROWSER_GUEST_HEADER "${MOBAGEN_BROWSER_GUEST_HEADER_DIR}/mobagen_smoke_guest_wasm.h")

find_program(EMCC_EXECUTABLE NAMES emcc NO_CACHE)
if(NOT EMCC_EXECUTABLE)
  # Fall back to the emsdk bundled in external/ (scripts/build.py layout).
  if(EXISTS "${CMAKE_SOURCE_DIR}/external/emsdk/upstream/emscripten/emcc")
    set(EMCC_EXECUTABLE "${CMAKE_SOURCE_DIR}/external/emsdk/upstream/emscripten/emcc")
  else()
    message(FATAL_ERROR "emcc not found: the browser backend needs the emscripten toolchain to build its reference guest")
  endif()
endif()

add_custom_command(
  OUTPUT "${MOBAGEN_BROWSER_GUEST_WASM}"
  COMMAND "${EMCC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_guest.c"
          "-I${CMAKE_SOURCE_DIR}/sdk/include"
          "-I${PROJECT_BINARY_DIR}/generated"
          -O1 -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=0 -Wl,--no-entry -Wl,--export-memory
          -Wl,--export=mobagen_wasm_plugin_allocate_v1 -Wl,--export=mobagen_wasm_plugin_deallocate_v1
          -Wl,--export=mobagen_wasm_plugin_query_v1 -Wl,--export=mobagen_wasm_plugin_configure_v1
          -Wl,--export=mobagen_wasm_plugin_start_v1 -Wl,--export=mobagen_wasm_plugin_quiesce_v1
          -Wl,--export=mobagen_wasm_plugin_stop_v1 -Wl,--export=mobagen_wasm_plugin_process_v1
          -Wl,--export=mobagen_smoke_add -Wl,--export=mobagen_module_entry_v1
          -Wl,--export=mobagen_module_exports_v1
          -o "${MOBAGEN_BROWSER_GUEST_WASM}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_guest.c"
  VERBATIM
  COMMENT "browser backend: compiling reference guest wasm"
)

# Byte-embed the guest into a generated header (no runtime file fetch needed
# by the smoke bundle; deployment loading uses the plugin store pipeline).
add_custom_command(
  OUTPUT "${MOBAGEN_BROWSER_GUEST_HEADER}"
  COMMAND ${CMAKE_COMMAND}
          "-DINPUT=${MOBAGEN_BROWSER_GUEST_WASM}"
          "-DOUTPUT=${MOBAGEN_BROWSER_GUEST_HEADER}"
          "-DSYMBOL=mobagen_smoke_guest_wasm"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_wasm.cmake"
  DEPENDS "${MOBAGEN_BROWSER_GUEST_WASM}"
  VERBATIM
  COMMENT "browser backend: embedding reference guest bytes"
)

# ---------------------------------------------------------------------------
# Backend library + smoke executable.
# ---------------------------------------------------------------------------
add_library(mobagen_browser_wasm_backend STATIC browser_wasm_backend.cpp browser_wasm_backend.hpp)
add_library(mobagen::browser_wasm_backend ALIAS mobagen_browser_wasm_backend)
target_compile_features(mobagen_browser_wasm_backend PUBLIC cxx_std_20)
target_link_libraries(mobagen_browser_wasm_backend PUBLIC mobagen::core_plugins)

# Node smoke executable (todo 8 test surface; BUILD_TESTING is OFF on web).
add_executable(MobagenBrowserBackendSmoke smoke_main.cpp "${MOBAGEN_BROWSER_GUEST_HEADER}")
target_compile_features(MobagenBrowserBackendSmoke PRIVATE cxx_std_20)
target_link_libraries(MobagenBrowserBackendSmoke PRIVATE mobagen::browser_wasm_backend)
target_include_directories(MobagenBrowserBackendSmoke PRIVATE "${MOBAGEN_BROWSER_GUEST_HEADER_DIR}")
# Headless node bundle: no DOM, no fetch; the runner boots it directly (and
# its boot worker installs the `window` SDL shim, so `web` must stay in the
# environment list or the runtime aborts). Guests stage onto MEMFS.
target_link_options(MobagenBrowserBackendSmoke PRIVATE -sENVIRONMENT=node,web -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1)
