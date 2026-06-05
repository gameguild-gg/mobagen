# ============================================================================
# Freetype — needed by RmlUi for font rendering.
#
#   * macOS / Linux / Windows desktop: rely on the system
#     find_package(Freetype) (libfreetype-dev on Linux, brew freetype
#     on macOS, system Freetype on Windows). RmlUi calls find_package
#     directly and we just need to make sure the target is findable.
#   * Emscripten: find_package can't find a target in the Emscripten
#     sysroot, so we create a Freetype::Freetype INTERFACE IMPORTED
#     target backed by Emscripten's built-in freetype port.
#   * Android: NDK does not ship Freetype, so we build from source via CPM.
#   * iOS: toolchain's CMAKE_FIND_ROOT_PATH is restricted to the iOS
#     SDK + sysroot (no Homebrew) so the system Freetype is invisible.
#     Build from source via CPM, just like Android.
# ============================================================================

if(EMSCRIPTEN)
  set(_EMS_SYSROOT "${EMSCRIPTEN_SYSROOT}")
  if(NOT _EMS_SYSROOT)
    if(DEFINED CMAKE_SYSROOT AND CMAKE_SYSROOT)
      set(_EMS_SYSROOT "${CMAKE_SYSROOT}")
    else()
      set(_EMS_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../external/emsdk/upstream/emscripten/cache/sysroot")
    endif()
  endif()

  set(_FT_INCLUDE_DIR "${_EMS_SYSROOT}/include/freetype2")
  set(_FT_LIBRARY     "${_EMS_SYSROOT}/lib/wasm32-emscripten/libfreetype.a")

  if(NOT EXISTS "${_FT_INCLUDE_DIR}/ft2build.h")
    message(WARNING
      "Freetype headers not found in Emscripten sysroot.\n"
      "  Run:  external/emsdk/upstream/emscripten/embuilder build freetype"
    )
  endif()

  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype INTERFACE IMPORTED)
    target_include_directories(Freetype::Freetype INTERFACE "${_FT_INCLUDE_DIR}")
    target_link_libraries(Freetype::Freetype INTERFACE
      "${_FT_LIBRARY}"
      "${_EMS_SYSROOT}/lib/wasm32-emscripten/libz.a"
    )
  endif()
elseif(ANDROID OR IOS)
  # Android NDK and the iOS toolchain sysroot both lack Freetype, and
  # the latter also restricts CMAKE_FIND_ROOT_PATH so Homebrew/system
  # Freetype is invisible to find_package. Build from source.
  if(NOT TARGET Freetype::Freetype)
    CPMAddPackage(
      NAME freetype
      GITHUB_REPOSITORY freetype/freetype
      GIT_TAG VER-2-13-3
      OPTIONS
        "FT_WITH_ZLIB OFF"
        "FT_WITH_PNG OFF"
        "FT_WITH_BZIP2 OFF"
        "FT_WITH_HARFBUZZ OFF"
        "FT_WITH_LZW OFF"
        "FT_DISABLE_TESTS ON"
    )

    if(TARGET freetype)
      add_library(Freetype::Freetype ALIAS freetype)
    endif()
  endif()
endif()
