# ============================================================================
# WebGPU via Dawn (native) + emdawnwebgpu (Emscripten / Web)
#
# Provides one interface target:
#
#   dawn::webgpu
#       - Native (macOS/Win/Linux): links Dawn (webgpu_dawn) and exposes
#         <webgpu/webgpu.h> + <webgpu/webgpu_cpp.h>.
#       - Emscripten: pulls in the emdawnwebgpu port from the Dawn tree
#         (replaces the deprecated `-sUSE_WEBGPU=1`) via `--use-port=...`.
#
# Notes:
#   * Both Dawn and emdawnwebgpu publish the same `<webgpu/webgpu_cpp.h>` API,
#     so ImGui's `imgui_impl_wgpu.cpp` compiles with the Dawn flavor on both.
#   * Building Dawn from source on native takes a while (~5-10 min cold).
# ============================================================================

add_library(dawn_webgpu INTERFACE)
add_library(dawn::webgpu ALIAS dawn_webgpu)

# ---------------------------------------------------------------------------
# Native-only build options. Must be set BEFORE CPMAddPackage so Dawn picks
# them up. On Emscripten we use DOWNLOAD_ONLY and skip these entirely.
# ---------------------------------------------------------------------------
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

  # Build only the backend we need per-platform.
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

# ---------------------------------------------------------------------------
# Single CPMAddPackage for both native and web. On Emscripten we only need the
# source tree (for the emdawnwebgpu port), so we skip building.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Wire the interface target according to platform.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  set(_EMDAWN_PORT "${dawn_SOURCE_DIR}/third_party/emdawnwebgpu/pkg/emdawnwebgpu.port.py")
  if(NOT EXISTS "${_EMDAWN_PORT}")
    message(FATAL_ERROR
      "emdawnwebgpu port not found at:\n  ${_EMDAWN_PORT}\n"
      "Check the Dawn tag pinned in external/dawn.cmake."
    )
  endif()
  message(STATUS "Using emdawnwebgpu port: ${_EMDAWN_PORT}")

  # The port replaces the old -sUSE_WEBGPU=1 path; emdawnwebgpu requires Asyncify.
  target_compile_options(dawn_webgpu INTERFACE
    "SHELL:--use-port=${_EMDAWN_PORT}"
  )
  target_link_options(dawn_webgpu INTERFACE
    "SHELL:--use-port=${_EMDAWN_PORT}"
    "SHELL:-sASYNCIFY"
  )
else()
  if(NOT TARGET webgpu_dawn)
    message(FATAL_ERROR
      "Dawn was added but target `webgpu_dawn` was not created. "
      "Check the Dawn tag / options pinned in external/dawn.cmake."
    )
  endif()

  target_link_libraries(dawn_webgpu INTERFACE webgpu_dawn)
  target_include_directories(dawn_webgpu INTERFACE
    "${dawn_SOURCE_DIR}/include"
    "${dawn_BINARY_DIR}/gen/include"
  )
  target_compile_definitions(dawn_webgpu INTERFACE
    WEBGPU_BACKEND_DAWN=1
  )
endif()
