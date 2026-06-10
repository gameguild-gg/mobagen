# ============================================================================
# SDL3 (static). Provides SDL3::SDL3-static.
#
# Bumped from SDL2 (release-2.32.8) to align with master's platform (SDL3 +
# Dawn WebGPU + ImGui). SDL3 hosts the native GL / WebGL2 context exactly as
# SDL2 did (SDL_GL_CreateContext, SDL_GL_SwapWindow, …); the renderer's SDL
# calls were migrated to the SDL3 API in apps/dicom_viewer/sources/main.cpp.
#
# SDL3_image is intentionally NOT pulled: the renderer reads volume.raw directly,
# so the dependency graph stays lean (no vendored opus -> no need for
# -fstack-protector-strong, which MSVC does not accept). Re-add SDL3_image here
# if image decoding is ever needed.
# ============================================================================
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
