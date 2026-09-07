cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

# Xcode 26 can default iOS targets to the active SDK version (for example 26.5) when no deployment
# target is set, which makes the generated project unusable on slightly older physical devices. Seed
# a safe default before project() so CMake propagates it to all iOS targets, while still allowing
# callers to override it.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET
      "15.0"
      CACHE STRING "Minimum supported iOS version"
  )
endif()

# CPM downloader
include(external/cpm.cmake)

# project declaration
project(
  MoBaGEn
  VERSION 0.0.1
  HOMEPAGE_URL "https://infinbrains.com"
  DESCRIPTION "Module Based Game Engine"
  LANGUAGES C CXX
)

# Include guards
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# compiler checks
include(external/compilerchecks.cmake)

# developer tooling (sanitizers / ccache / static analyzers) — no-op for normal builds
include(cmake/tools.cmake)

# set output dirs
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# set linkage
link_directories(${CMAKE_BINARY_DIR}/libs)
# LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/libs/Debug)

if(IOS)
  # IOS_BUNDLE_ID / IOS_DEVELOPMENT_TEAM can be overridden with -D on the cmake command line.
  # build.py passes them via --ios-bundle-id and --ios-team-id.
  set(IOS_BUNDLE_ID
      "gg.gameguild.mobagen"
      CACHE STRING "Bundle identifier for iOS app targets"
  )
  set(IOS_DEVELOPMENT_TEAM
      ""
      CACHE STRING "Apple development team ID (10-char alphanumeric, e.g. ABCDE12345)"
  )
  set(IOS_CODE_SIGN_STYLE
      "Automatic"
      CACHE STRING "Xcode code signing style for iOS device builds"
  )
  set(IOS_DEPLOYMENT_TARGET
      "${CMAKE_OSX_DEPLOYMENT_TARGET}"
      CACHE STRING "Minimum iOS version for generated Xcode targets"
  )

  set(CMAKE_OSX_DEPLOYMENT_TARGET
      "${IOS_DEPLOYMENT_TARGET}"
      CACHE STRING "Minimum supported iOS version" FORCE
  )

  # Propagate settings to all generated Xcode targets. CMAKE_XCODE_ATTRIBUTE_* is the CMake
  # mechanism for project-level Xcode build settings.
  set(CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${IOS_BUNDLE_ID}")
  set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${IOS_DEPLOYMENT_TARGET}")
  # Always write DEVELOPMENT_TEAM (even as empty string) so stale values from previous configure
  # runs don't persist in the re-generated Xcode project.
  set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}")
  if(NOT CMAKE_OSX_SYSROOT STREQUAL "iphonesimulator")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "${IOS_CODE_SIGN_STYLE}")
  endif()
  message(STATUS "iOS bundle ID  : ${IOS_BUNDLE_ID}")
  message(STATUS "iOS team ID    : ${IOS_DEVELOPMENT_TEAM}")
  message(STATUS "iOS deploy tgt : ${IOS_DEPLOYMENT_TARGET}")
endif()

if(EMSCRIPTEN)
  message(STATUS "emscripten build")
  # NOTE: We do NOT pass -sUSE_WEBGPU=1 -- that emscripten port is deprecated. WebGPU comes from the
  # emdawnwebgpu port wired up by external/dawn.cmake (it injects
  # --use-port=.../emdawnwebgpu.port.py on the link line). SDL3 is built from source, so -sUSE_SDL=0
  # keeps emscripten from bundling its own. NOTE: WASM=2 (wasm + wasm2js JS fallback) was dropped:
  # wasm2js was removed from modern Binaryen/Emscripten toolchains and the wasm2js binary shipped
  # with current emsdk crashes (SIGABRT) at link time. Every WebGPU-capable browser supports WASM,
  # so the JS fallback served no purpose here. -pthread: the emdawnwebgpu port links with shared
  # memory, which requires all objects to be compiled with atomics + bulk-memory features.
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -Oz -s USE_PTHREADS=0 -sSHARED_MEMORY=0 -s USE_SDL=0 -s ASYNCIFY -s ASSERTIONS=1 -s ALLOW_MEMORY_GROWTH=1"
  )
  set(CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} -Oz -s USE_PTHREADS=0 -sSHARED_MEMORY=0 -s USE_SDL=0 -s ASYNCIFY -s ASSERTIONS=1 -s ALLOW_MEMORY_GROWTH=1"
  )

  file(GLOB HTML "${CMAKE_CURRENT_SOURCE_DIR}/htmls/*")
  file(COPY ${HTML} DESTINATION ${CMAKE_BINARY_DIR}/bin/)

  set(ENABLE_TEST_COVERAGE OFF)
endif()

include(cmake/subdirlist.cmake)

# Medical-imaging library options (heavy source builds; OFF by default)
option(USE_GDCM "Build GDCM (DICOM parsing) for the volume_io module" OFF)
option(USE_ITK "Build ITK (medical image processing; bundles GDCM)" OFF)
option(USE_VTK "Build VTK (visualization toolkit; reference only)" OFF)

# App-group build switches
option(BUILD_CORE_DEMOS "Build engine learning demos under apps/core_demos" ON)
option(BUILD_VOLUME_DEMOS "Build volume smoke tests under apps/volume_demos" ON)
option(BUILD_DICOM_VIEWER "Build the DICOM viewer app" ON)
option(BUILD_PROTOTYPES "Build learning prototypes (dawn_probe, fibers)" OFF)

include(external/external.cmake)

if(USE_GDCM)
  include(external/gdcm.cmake)
endif()
if(USE_ITK)
  include(external/itk.cmake)
endif()
if(USE_VTK)
  include(external/vtk.cmake)
endif()

# GLM must be available before core/ — all DOD headers use it globally
include(external/glm.cmake)

# doctest must be available before test/ and apps/ formal test runners
include(external/doctest.cmake)

# Format.cmake (format-cmake targets) for test/ and style CI
include(external/format-cmake.cmake)

add_subdirectory(core)
add_subdirectory(modules)

option(ENABLE_TEST_COVERAGE "ENABLE_TEST_COVERAGE" ON)
if(ENABLE_TEST_COVERAGE)
  add_subdirectory(test)
endif()

option(BUILD_EXAMPLES "BUILD_EXAMPLES" ON)
if(BUILD_EXAMPLES)
  add_subdirectory(apps)
endif()

option(ENABLE_DOCUMENTATION "ENABLE_DOCUMENTATION" OFF)
if(ENABLE_DOCUMENTATION)
  add_subdirectory(documentation)
endif()

CPMAddPackage(
  NAME CPMLicenses.cmake
  GITHUB_REPOSITORY cpm-cmake/CPMLicenses.cmake
  VERSION 0.0.5
)
write_license_disclaimer("third_party.txt" "${CPM_PACKAGES}")

# Android: create a SHARED library from the same sources as the selected app target, named
# "main" (→ libmain.so) so SDLActivity can load it via System.loadLibrary("main"). Apps are
# normally built with `add_executable`, but the NDK toolchain produces those as PIE binaries with
# no exported text symbols — which breaks SDLActivity's dlsym("SDL_main"). Building a separate
# SHARED library target from the same files fixes this without modifying any per-app CMakeLists.
#
# Entry chain (SDL3 callback model): SDLActivity calls nativeRunMain("SDL_main"), which dlsym's
# SDL_main in libmain.so (SDL3 src/core/android/SDL_android.c). On Android SDL_main.h sets
# SDL_MAIN_EXPORTED and its impl header DEFINES the exported SDL_main under
# SDL_MAIN_USE_CALLBACKS (SDL_main_impl.h) — that definition is synthesized in
# core/sources/app/sdl_app.cpp, the single TU including the SDL3 main header; MOBAGEN_MAIN apps
# define neither SDL_main nor main(). The injected android_main_wrapper.cpp therefore no longer
# forwards SDL_main -> main(); it is a pure anchor whose reference to SDL_main drags sdl_app.o
# out of libmobagen_core_app.a into the shared library (a shared-lib link has no undefined
# `main` to pull the archive member, so without the anchor libmain.so would silently export no
# SDL_main and dlsym would fail at app launch).
#
# Set -DANDROID_MAIN_TARGET=<app_name> when configuring for Android.
if(ANDROID AND DEFINED ANDROID_MAIN_TARGET)
  if(TARGET ${ANDROID_MAIN_TARGET})
    get_target_property(_orig_src ${ANDROID_MAIN_TARGET} SOURCES)
    get_target_property(_inc_dirs ${ANDROID_MAIN_TARGET} INCLUDE_DIRECTORIES)
    get_target_property(_link_libs ${ANDROID_MAIN_TARGET} LINK_LIBRARIES)
    get_target_property(_comp_defs ${ANDROID_MAIN_TARGET} COMPILE_DEFINITIONS)
    get_target_property(_comp_opts ${ANDROID_MAIN_TARGET} COMPILE_OPTIONS)
    get_target_property(_link_opts ${ANDROID_MAIN_TARGET} LINK_OPTIONS)

    set(_shim_name "${ANDROID_MAIN_TARGET}_android_so")
    add_library(${_shim_name} SHARED ${_orig_src})

    if(_inc_dirs)
      target_include_directories(${_shim_name} PUBLIC ${_inc_dirs})
    endif()
    if(_link_libs)
      target_link_libraries(${_shim_name} PUBLIC ${_link_libs})
    endif()
    if(_comp_defs)
      target_compile_definitions(${_shim_name} PUBLIC ${_comp_defs})
    endif()
    if(_comp_opts)
      target_compile_options(${_shim_name} PRIVATE ${_comp_opts})
    endif()
    if(_link_opts)
      target_link_options(${_shim_name} PRIVATE ${_link_opts})
    endif()

    set_target_properties(${_shim_name} PROPERTIES OUTPUT_NAME "main")
    target_sources(
      ${_shim_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core/sources/android_main_wrapper.cpp"
    )

    # Empty out the original EXECUTABLE's sources so it doesn't try to build a second binary.
    # add_executable validates SOURCES at the `add` call, so we have to also patch the example's
    # CMakeLists to skip the add_executable line — done via the ANDROID_SKIP_EXECUTABLE variable
    # each example can opt into. Since we don't want to touch the per-example files, instead just
    # add a no-op source so the original target still has *something* to build.
    set_target_properties(
      ${ANDROID_MAIN_TARGET} PROPERTIES OUTPUT_NAME "${ANDROID_MAIN_TARGET}_disabled"
                                        EXCLUDE_FROM_ALL TRUE
    )

    message(
      STATUS "Android: '${ANDROID_MAIN_TARGET}' will be packaged as libmain.so (via ${_shim_name})"
    )
  else()
    message(WARNING "ANDROID_MAIN_TARGET '${ANDROID_MAIN_TARGET}' is not a known target. "
                    "Available examples: check apps/ subdirectories."
    )
  endif()
endif()

# todo: follow https://github.com/TheLartians/ModernCppStarter
