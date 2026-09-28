# QuickJS guest size assert (todo 16): the produced plugin.wasm must stay
# under the Chromium main-thread sync-compile budget (8 MiB, review
# rr-dlap-1) — the browser sync path THROWS BackendFailure at that limit, so
# the build fails here instead, loudly. Script mode:
#   cmake -DMOBAGEN_QUICKJS_WASM=<path> -DMOBAGEN_QUICKJS_MAX_BYTES=<n> -P ...
if(NOT DEFINED MOBAGEN_QUICKJS_WASM OR NOT DEFINED MOBAGEN_QUICKJS_MAX_BYTES)
  message(FATAL_ERROR "quickjs_size_check.cmake: MOBAGEN_QUICKJS_WASM and MOBAGEN_QUICKJS_MAX_BYTES are required")
endif()
if(NOT EXISTS "${MOBAGEN_QUICKJS_WASM}")
  message(FATAL_ERROR "quickjs_size_check.cmake: '${MOBAGEN_QUICKJS_WASM}' does not exist")
endif()
file(SIZE "${MOBAGEN_QUICKJS_WASM}" MOBAGEN_QUICKJS_ACTUAL_BYTES)
if(MOBAGEN_QUICKJS_ACTUAL_BYTES GREATER MOBAGEN_QUICKJS_MAX_BYTES)
  message(FATAL_ERROR
    "QuickJS guest plugin.wasm is ${MOBAGEN_QUICKJS_ACTUAL_BYTES} bytes — exceeds the ${MOBAGEN_QUICKJS_MAX_BYTES}-byte "
    "Chromium sync-compile budget. Shrink the build (drop quickjs features / -Oz) before shipping.")
endif()
message(STATUS "QuickJS guest size ok: ${MOBAGEN_QUICKJS_ACTUAL_BYTES} bytes (limit ${MOBAGEN_QUICKJS_MAX_BYTES})")
