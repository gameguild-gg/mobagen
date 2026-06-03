# RmlUi - HTML/CSS User Interface Library
# Uses the official mikke89/RmlUi repo via CPM.
# We build the core libraries and compile the SDL3 platform backend ourselves.
# The WebGPU render backend is implemented in core/RmlUiWgpuRenderer.

set(RMLUI_VERSION 6.2)

# RmlUi needs FreeType for font rendering. It bundles FreeType in its
# Dependencies/ directory and will find it there automatically because
# the root CMakeLists.txt adds Dependencies/ to CMAKE_PREFIX_PATH.
# We must set this BEFORE CPMAddPackage so the options take effect.
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "")
set(RMLUI_SAMPLES OFF CACHE BOOL "")
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "")
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "")
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "")
set(RMLUI_THIRDPARTY_CONTAINERS ON CACHE BOOL "")
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "")
set(RMLUI_WARNINGS_AS_ERRORS OFF CACHE BOOL "")

# Disable samples backend selection (we don't build samples)
set(RMLUI_BACKEND "auto" CACHE STRING "")

CPMAddPackage(
  NAME RmlUi
  GIT_TAG 6.2
  GITHUB_REPOSITORY mikke89/RmlUi
)

# Create an aggregate target that includes core + controls + debugger
if(TARGET rmlui_core AND TARGET rmlui_debugger)
  # Add the SDL3 platform backend. We compile RmlUi's SDL platform file
  # directly as part of a custom static library.
  add_library(rmlui_platform_sdl STATIC)

  set(RMLUI_SDL_PLATFORM_DIR ${RmlUi_SOURCE_DIR}/Backends)

  target_sources(rmlui_platform_sdl PRIVATE
    ${RMLUI_SDL_PLATFORM_DIR}/RmlUi_Platform_SDL.cpp
  )

  target_include_directories(rmlui_platform_sdl PUBLIC
    ${RMLUI_SDL_PLATFORM_DIR}
    ${RmlUi_SOURCE_DIR}/Include
  )

  target_compile_definitions(rmlui_platform_sdl PUBLIC
    RMLUI_SDL_VERSION_MAJOR=3
    RMLUI_STATIC_LIB
  )

  target_link_libraries(rmlui_platform_sdl PUBLIC
    rmlui_core
    SDL3::SDL3-static
  )

  # Aggregate convenience target for linking by demos/examples.
  # Not a real library — just collects all RmlUi pieces.
  add_library(rmlui_all INTERFACE)
  target_link_libraries(rmlui_all INTERFACE
    rmlui_core
    rmlui_debugger
    rmlui_platform_sdl
  )
  # Expose the Debugger source directory so consumers can include
  # FontSource.h (the embedded Courier Prime Code font used by the
  # RmlUi debugger). This lets demos use the same font RmlUi ships
  # with — no extra font files needed in the repo.
  #
  # NOTE: `Source/Debugger/FontSource.h` is an INTERNAL upstream file.
  # The path/contents can change in any RmlUi release. The pin on
  # `GIT_TAG 6.2` above is the only thing keeping this working; when
  # bumping RmlUi, check that FontSource.h still exposes the same
  # `courier_prime_code[]` / `courier_prime_code_italic[]` arrays, or
  # remove this include and switch to a different embedded font.
  target_include_directories(rmlui_all INTERFACE
    ${RmlUi_SOURCE_DIR}/Include
    ${RmlUi_SOURCE_DIR}/Source/Debugger
    ${RMLUI_SDL_PLATFORM_DIR}
  )

  message(STATUS "RmlUi ${RMLUI_VERSION} configured (core + debugger + SDL3 platform)")
else()
  message(WARNING "RmlUi targets not found — skipping RmlUi configuration")
endif()
