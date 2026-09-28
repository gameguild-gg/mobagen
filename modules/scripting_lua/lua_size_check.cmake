# Lua guest size assert (todo 17): the produced plugin.wasm must stay
# under the Chromium main-thread sync-compile budget (8 MiB, review
# rr-dlap-1) — parity with quickjs_size_check.cmake (todo 16). The
# browser sync path THROWS BackendFailure at that limit; this assert makes
# an oversized guest a BUILD failure instead. Script mode:
#   cmake -DMOBAGEN_LUA_WASM=<path> -DMOBAGEN_LUA_MAX_BYTES=<n> -P ...
if(NOT DEFINED MOBAGEN_LUA_WASM OR NOT DEFINED MOBAGEN_LUA_MAX_BYTES)
  message(FATAL_ERROR "lua_size_check.cmake: MOBAGEN_LUA_WASM and MOBAGEN_LUA_MAX_BYTES are required")
endif()
if(NOT EXISTS "${MOBAGEN_LUA_WASM}")
  message(FATAL_ERROR "lua_size_check.cmake: '${MOBAGEN_LUA_WASM}' does not exist")
endif()
file(SIZE "${MOBAGEN_LUA_WASM}" MOBAGEN_LUA_ACTUAL_BYTES)
if(MOBAGEN_LUA_ACTUAL_BYTES GREATER MOBAGEN_LUA_MAX_BYTES)
  message(FATAL_ERROR
    "Lua guest plugin.wasm is ${MOBAGEN_LUA_ACTUAL_BYTES} bytes — exceeds the ${MOBAGEN_LUA_MAX_BYTES}-byte "
    "Chromium sync-compile budget. Shrink the build (drop library modules / -Oz) before shipping.")
endif()
message(STATUS "Lua guest size ok: ${MOBAGEN_LUA_ACTUAL_BYTES} bytes (limit ${MOBAGEN_LUA_MAX_BYTES})")
