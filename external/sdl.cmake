# ============================================================================
# SDL3 + SDL3_image
#
# Provides (desktop / Emscripten): SDL3::SDL3-static          (linked by `core`, transitively by
# examples/editor) SDL3_image::SDL3_image-static
#
# Provides (Android): SDL3::SDL3-shared          (Android requires a shared library loaded by
# SDLActivity)
# ============================================================================

if(NOT DEFINED EMSCRIPTEN)
  # required by SDL3 vendored deps (e.g. opus) on some platforms
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
endif()

# Android needs SDL3 as a shared library so SDLActivity can load it via JNI. All other platforms use
# the static library.
if(ANDROID)
  set(_sdl_shared ON)
  set(_sdl_static OFF)
else()
  set(_sdl_shared OFF)
  set(_sdl_static ON)
endif()

# ---- SDL3 ------------------------------------------------------------------
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME SDL3
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_TAG release-3.4.0
  OPTIONS "SDL_DISABLE_INSTALL ON"
          "SDL_SHARED ${_sdl_shared}"
          "SDL_STATIC ${_sdl_static}"
          "SDL_STATIC_PIC ON"
          "SDL_WERROR OFF"
          "SDL_TEST_LIBRARY OFF"
          "SDL_TESTS OFF"
          "SDL_DIRECTX OFF"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTASDL "${AFTER} - ${BEFORE}")
message(STATUS "SDL3 TIME: ${DELTASDL}s")

# ---- SDL3_image ------------------------------------------------------------
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME SDL3_image
  GITHUB_REPOSITORY libsdl-org/SDL_image
  GIT_TAG release-3.4.0
  OPTIONS "BUILD_SHARED_LIBS OFF"
          "SDL3IMAGE_INSTALL OFF"
          "SDL3IMAGE_SAMPLES OFF"
          "SDL3IMAGE_VENDORED ON"
          "SDL3IMAGE_DEPS_SHARED OFF"
          # AVIF off: vendored libavif pulls in dav1d, which requires NASM on Windows runners (no
          # CMAKE_ASM_NASM_COMPILER -> configure fails). The engine does not use AVIF images.
          "SDLIMAGE_AVIF OFF"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTASDL_image "${AFTER} - ${BEFORE}")
message(STATUS "SDL3_image TIME: ${DELTASDL_image}s")
