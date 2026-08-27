# RmlUi - HTML/CSS User Interface Library Uses the official mikke89/RmlUi repo via CPM. We build the
# core libraries and compile the SDL3 platform backend ourselves. The WebGPU render backend is
# implemented in core/RmlUiWgpuRenderer.

set(RMLUI_VERSION 6.2)

# RmlUi requires FreeType for font rendering. On Emscripten, freetype is shipped as a port (built on
# demand into the sysroot) rather than as part of the base toolchain. RmlUi's CMake imports
# `Freetype::Freetype` and validates the INTERFACE_INCLUDE_DIRECTORIES path at configure time, so
# the port must exist BEFORE CPMAddPackage(RmlUi) runs. Trigger embuilder here rather than expecting
# the user (or build.py) to remember it.
if(EMSCRIPTEN)
  find_program(
    EMBUILDER
    NAMES embuilder embuilder.py
    PATHS ENV EMSDK ENV EMSCRIPTEN_ROOT
    PATH_SUFFIXES upstream/emscripten
  )
  if(NOT EMBUILDER)
    get_filename_component(_emcc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(
      EMBUILDER
      NAMES embuilder embuilder.py
      PATHS "${_emcc_dir}"
      NO_DEFAULT_PATH
    )
  endif()
  if(EMBUILDER)
    set(_freetype_sysroot_lib
        "$ENV{EMSDK}/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/libfreetype.a"
    )
    if(NOT EXISTS "${_freetype_sysroot_lib}")
      execute_process(COMMAND "${EMBUILDER}" build freetype RESULT_VARIABLE _ft_rc)
      if(NOT _ft_rc EQUAL 0)
        message(FATAL_ERROR "embuilder build freetype failed (exit ${_ft_rc})")
      endif()
    endif()
  else()
    message(WARNING "EMBUILDER not found; freetype port may need manual build")
  endif()
endif()

set(RMLUI_FONT_ENGINE
    "freetype"
    CACHE STRING ""
)
set(RMLUI_SAMPLES
    OFF
    CACHE BOOL ""
)
set(RMLUI_LUA_BINDINGS
    OFF
    CACHE BOOL ""
)
set(RMLUI_LOTTIE_PLUGIN
    OFF
    CACHE BOOL ""
)
set(RMLUI_SVG_PLUGIN
    OFF
    CACHE BOOL ""
)
set(RMLUI_THIRDPARTY_CONTAINERS
    ON
    CACHE BOOL ""
)
set(RMLUI_PRECOMPILED_HEADERS
    OFF
    CACHE BOOL ""
)
set(RMLUI_WARNINGS_AS_ERRORS
    OFF
    CACHE BOOL ""
)

set(RMLUI_BACKEND
    "auto"
    CACHE STRING ""
)

CPMAddPackage(
  NAME RmlUi
  GIT_TAG 6.2
  GITHUB_REPOSITORY mikke89/RmlUi
)

# Create an aggregate target that includes core + controls + debugger
if(TARGET rmlui_core AND TARGET rmlui_debugger)
  # Add the SDL3 platform backend. We compile RmlUi's SDL platform file directly as part of a custom
  # static library.
  add_library(rmlui_platform_sdl STATIC)

  set(RMLUI_SDL_PLATFORM_DIR ${RmlUi_SOURCE_DIR}/Backends)

  target_sources(rmlui_platform_sdl PRIVATE ${RMLUI_SDL_PLATFORM_DIR}/RmlUi_Platform_SDL.cpp)

  target_include_directories(
    rmlui_platform_sdl PUBLIC ${RMLUI_SDL_PLATFORM_DIR} ${RmlUi_SOURCE_DIR}/Include
  )

  target_compile_definitions(rmlui_platform_sdl PUBLIC RMLUI_SDL_VERSION_MAJOR=3 RMLUI_STATIC_LIB)

  # Android uses the shared SDL3 (loaded by SDLActivity via JNI); other platforms use the static
  # variant.
  if(ANDROID)
    set(_RMLUI_SDL_TARGET SDL3::SDL3-shared)
  else()
    set(_RMLUI_SDL_TARGET SDL3::SDL3-static)
  endif()

  target_link_libraries(rmlui_platform_sdl PUBLIC rmlui_core ${_RMLUI_SDL_TARGET})

  # Aggregate convenience target for linking by demos/examples. Not a real library — just collects
  # all RmlUi pieces.
  add_library(rmlui_all INTERFACE)
  target_link_libraries(rmlui_all INTERFACE rmlui_core rmlui_debugger rmlui_platform_sdl)
  # Generate a forwarding header so the internal RmlUi debugger file `Source/Debugger/FontSource.h`
  # can be included by consumers as <RmlUi/Debugger/FontSource.h> — matching the RmlUi convention
  # (e.g. <RmlUi/Core.h>, <RmlUi/Debugger.h>) without leaking the private `Source/` tree onto the
  # include path.
  set(_font_forward_dir "${CMAKE_CURRENT_BINARY_DIR}/rmlui_gen/RmlUi/Debugger")
  file(MAKE_DIRECTORY "${_font_forward_dir}")
  file(WRITE "${_font_forward_dir}/FontSource.h"
       "#include \"${RmlUi_SOURCE_DIR}/Source/Debugger/FontSource.h\"\n"
  )

  target_include_directories(
    rmlui_all INTERFACE ${RmlUi_SOURCE_DIR}/Include "${CMAKE_CURRENT_BINARY_DIR}/rmlui_gen"
                        ${RMLUI_SDL_PLATFORM_DIR}
  )

  # NOTE: `Source/Debugger/FontSource.h` is an INTERNAL upstream file. The path/contents can change
  # in any RmlUi release. The pin on `GIT_TAG 6.2` above is the only thing keeping this working;
  # when bumping RmlUi, check that FontSource.h still exposes the same `courier_prime_code[]` /
  # `courier_prime_code_italic[]` arrays, or remove this include and switch to a different embedded
  # font.

  message(STATUS "RmlUi ${RMLUI_VERSION} configured (core + debugger + SDL3 platform)")
else()
  message(WARNING "RmlUi targets not found — skipping RmlUi configuration")
endif()
