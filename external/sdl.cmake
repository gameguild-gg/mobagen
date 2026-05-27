# ============================================================================
# SDL3 + SDL3_image
#
# Provides:
#   SDL3::SDL3-static          (linked by `core`, transitively by examples/editor)
#   SDL3_image::SDL3_image-static
# ============================================================================

if(NOT DEFINED EMSCRIPTEN)
  # required by SDL3 vendored deps (e.g. opus) on some platforms
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -fstack-protector-strong")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
endif()

# ---- SDL3 ------------------------------------------------------------------
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME SDL3
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_TAG release-3.4.0
  OPTIONS
    "SDL_DISABLE_INSTALL ON"
    "SDL_SHARED OFF"
    "SDL_STATIC ON"
    "SDL_STATIC_PIC ON"
    "SDL_WERROR OFF"
    "SDL_TEST_LIBRARY OFF"
    "SDL_TESTS OFF"
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
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "SDL3IMAGE_INSTALL OFF"
    "SDL3IMAGE_SAMPLES OFF"
    "SDL3IMAGE_VENDORED ON"
    "SDL3IMAGE_DEPS_SHARED OFF"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTASDL_image "${AFTER} - ${BEFORE}")
message(STATUS "SDL3_image TIME: ${DELTASDL_image}s")
