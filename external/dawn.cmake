# ============================================================================
# WebGPU via Dawn (native) + emdawnwebgpu (Emscripten / Web)   [from master]
#
# Provides one interface target:
#   mobagen::webgpu
#       - Native (macOS/Win/Linux): links Dawn (webgpu_dawn) and exposes
#         <webgpu/webgpu.h> + <webgpu/webgpu_cpp.h>.
#       - Emscripten: pulls in the emdawnwebgpu port from the Dawn tree
#         (replaces the deprecated `-sUSE_WEBGPU=1`) via `--use-port=...`.
#
# Notes:
#   * Both Dawn and emdawnwebgpu publish the same <webgpu/webgpu_cpp.h> API.
#   * Building Dawn from source on native takes a while (~5-10 min cold) and
#     fetches gigabytes of its own dependencies (DAWN_FETCH_DEPENDENCIES).
# ============================================================================

add_library(mobagen_webgpu INTERFACE)
add_library(mobagen::webgpu ALIAS mobagen_webgpu)

if(NOT EMSCRIPTEN)
  set(DAWN_FETCH_DEPENDENCIES        ON  CACHE BOOL "" FORCE)
  set(DAWN_ENABLE_INSTALL            OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_SAMPLES             OFF CACHE BOOL "" FORCE)
  set(DAWN_USE_GLFW                  OFF CACHE BOOL "" FORCE)
  set(DAWN_USE_WAYLAND               OFF CACHE BOOL "" FORCE)
  if(APPLE OR WIN32)
    set(DAWN_USE_X11                 OFF CACHE BOOL "" FORCE)
  else()
    set(DAWN_USE_X11                 ON  CACHE BOOL "" FORCE)
  endif()
  set(TINT_BUILD_TESTS               OFF CACHE BOOL "" FORCE)
  set(TINT_BUILD_CMD_TOOLS           OFF CACHE BOOL "" FORCE)
  set(TINT_BUILD_DOCS                OFF CACHE BOOL "" FORCE)
  set(DAWN_BUILD_TESTS               OFF CACHE BOOL "" FORCE)

  if(APPLE)
    set(DAWN_ENABLE_METAL      ON  CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_VULKAN     OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D12      OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES   OFF CACHE BOOL "" FORCE)
  elseif(WIN32)
    set(DAWN_ENABLE_D3D12      ON  CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_VULKAN     OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_METAL      OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES   OFF CACHE BOOL "" FORCE)
  else() # Linux/BSD
    set(DAWN_ENABLE_VULKAN     ON  CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_METAL      OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D12      OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES   OFF CACHE BOOL "" FORCE)
  endif()
endif()

if(EMSCRIPTEN)
  set(_DAWN_DOWNLOAD_ONLY YES)
else()
  set(_DAWN_DOWNLOAD_ONLY NO)
endif()

string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME dawn
  VERSION 20260423.175430
  URL https://github.com/google/dawn/archive/refs/tags/v20260423.175430.tar.gz
  DOWNLOAD_ONLY ${_DAWN_DOWNLOAD_ONLY}
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTADAWN "${AFTER} - ${BEFORE}")
message(STATUS "Dawn fetch/configure TIME: ${DELTADAWN}s")

if(EMSCRIPTEN)
  # emdawnwebgpu's IN-TREE port.py refuses standalone use ("must sit in a built
  # emdawnwebgpu_pkg"). em++ requires the BUILT package, which Dawn publishes as
  # a release asset (emdawnwebgpu_pkg-<tag>.zip). Fetch + extract it and point
  # --use-port at its emdawnwebgpu.port.py. The pkg is self-contained (ships the
  # webgpu headers + JS), so the Dawn source tarball above isn't used on web.
  CPMAddPackage(
    NAME emdawnwebgpu_pkg
    VERSION 20260423.175430
    URL https://github.com/google/dawn/releases/download/v20260423.175430/emdawnwebgpu_pkg-v20260423.175430.zip
    DOWNLOAD_ONLY YES
  )
  file(GLOB_RECURSE _EMDAWN_PORTS "${emdawnwebgpu_pkg_SOURCE_DIR}/*emdawnwebgpu.port.py")
  if(NOT _EMDAWN_PORTS)
    message(FATAL_ERROR
      "emdawnwebgpu.port.py not found in fetched emdawnwebgpu_pkg "
      "(${emdawnwebgpu_pkg_SOURCE_DIR}). Check the release asset URL/tag.")
  endif()
  list(GET _EMDAWN_PORTS 0 _EMDAWN_PORT)
  message(STATUS "Using emdawnwebgpu port: ${_EMDAWN_PORT}")
  target_compile_options(mobagen_webgpu INTERFACE "SHELL:--use-port=${_EMDAWN_PORT}")
  target_link_options(mobagen_webgpu INTERFACE
    "SHELL:--use-port=${_EMDAWN_PORT}"
    "SHELL:-sASYNCIFY")
else()
  if(NOT TARGET webgpu_dawn)
    message(FATAL_ERROR
      "Dawn was added but target `webgpu_dawn` was not created. "
      "Check the Dawn tag / options pinned in external/dawn.cmake.")
  endif()
  target_link_libraries(mobagen_webgpu INTERFACE webgpu_dawn)
  target_include_directories(mobagen_webgpu INTERFACE
    "${dawn_SOURCE_DIR}/include"
    "${dawn_BINARY_DIR}/gen/include")
  target_compile_definitions(mobagen_webgpu INTERFACE WEBGPU_BACKEND_DAWN=1)
endif()
