if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

# ---------------------------------------------------------------------------
# Freetype — needed by RmlUi for font rendering.
#   * Native: rely on the system find_package(Freetype) (libfreetype-dev on
#     Linux, brew freetype on macOS, system Freetype on Windows).
#   * Emscripten: find_package can't find a target in the Emscripten
#     sysroot, so we create a Freetype::Freetype INTERFACE IMPORTED target
#     backed by Emscripten's built-in freetype port.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  set(_EMS_SYSROOT "${EMSCRIPTEN_SYSROOT}")
  if(NOT _EMS_SYSROOT)
    # Emscripten sets CMAKE_SYSROOT to the cache sysroot path
    if(DEFINED CMAKE_SYSROOT AND CMAKE_SYSROOT)
      set(_EMS_SYSROOT "${CMAKE_SYSROOT}")
    else()
      # Fallback: relative to emcmake
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
      # Freetype's ftgzip.c uses zlib (inflate*) — must be linked explicitly
      # because the Emscripten sysroot doesn't auto-link it.
      "${_EMS_SYSROOT}/lib/wasm32-emscripten/libz.a"
    )
  endif()
endif()

if(EMSCRIPTEN)

elseif(ANDROID)
  # set( SDL_STATIC ON CACHE BOOL "Build the static SDL library" ) set( SDL_SHARED OFF CACHE BOOL
  # "Build the shared SDL library" ) # set( SDL_FILESYSTEM FALSE ) set( PTHREADS OFF CACHE BOOL
  # "Pthread support" ) add_definitions(-DGL_GLEXT_PROTOTYPES)
else()
  # INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/) IF (WIN32) add_library (whereami
  # STATIC ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.c
  # ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.h) ELSE () add_library (whereami SHARED
  # ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.c
  # ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.h) ENDIF () set( SDL_STATIC OFF CACHE BOOL
  # "Build the static SDL library" ) set( SDL_SHARED ON CACHE BOOL "Build the shared SDL library" )

  # set( glew-cmake_BUILD_SHARED ON CACHE BOOL "Build the shared glew library" ) set(
  # glew-cmake_BUILD_STATIC OFF CACHE BOOL "Build the static glew library" ) set(
  # glew-cmake_BUILD_SINGLE_CONTEXT ON CACHE BOOL "Build the single context glew library" ) set(
  # glew-cmake_BUILD_MULTI_CONTEXT OFF CACHE BOOL "Build the multi context glew library" )
  #
  # add_subdirectory( glew )
endif()

# include(external/quickjs.cmake) if(NOT EMSCRIPTEN) IF(APPLE) set(CMAKE_THREAD_LIBS_INIT
# "-lpthread") set(CMAKE_HAVE_THREADS_LIBRARY 1) set(CMAKE_USE_WIN32_THREADS_INIT 0)
# set(CMAKE_USE_PTHREADS_INIT 1) set(THREADS_PREFER_PTHREAD_FLAG ON) ENDIF() find_package(Threads
# REQUIRED) include(external/wasm.cmake) include(external/v8.cmake) endif()

# include(external/quickjs.cmake) include(filament.cmake) include(threadpool.cmake)
# include(external/zlib.cmake)
include(external/sdl.cmake)
include(external/dawn.cmake)
# include(glm.cmake) include(glew.cmake)
include(external/imgui.cmake)
include(external/rmlui.cmake)
# if(NOT EMSCRIPTEN) include(external/mbedtls.cmake) include(external/curl.cmake)
# include(external/cpr.cmake) endif() include(assimp.cmake) include(bullet.cmake)
