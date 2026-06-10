cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include(external/cpm.cmake)

project(DicomRenderer VERSION 0.1.0 LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

set(CXX_STANDARD_TARGET "23" CACHE STRING "CXX standard" FORCE)
include(external/compilerchecks.cmake)

# Opt-in developer tooling (sanitizers / static analyzers / ccache), brought over
# from master (cmake/tools.cmake). No-op for a normal build — only fires when
# -DUSE_SANITIZER=Address (etc.), -DUSE_STATIC_ANALYZER=clang-tidy, or
# -DUSE_CCACHE=YES is passed. ASan here would have caught the keys_pressed_ OOB.
include(cmake/tools.cmake)

# ----------------------------------------------------------------------------
# Project build switches.
# ----------------------------------------------------------------------------
set(DEFAULT_USE_WEBGPU OFF)
if(APPLE AND NOT EMSCRIPTEN)
    # OpenGL is deprecated on macOS; default to Dawn/Metal for native builds.
    set(DEFAULT_USE_WEBGPU ON)
endif()
option(USE_WEBGPU "Build the WebGPU (G3) renderer instead of WebGL2 (G2)" ${DEFAULT_USE_WEBGPU})
option(BUILD_RENDERER "Build the dicom_renderer executable" ON)

# ----------------------------------------------------------------------------
# Medical-imaging libraries (brought over from master). These are HEAVY source
# builds, so each is gated behind an option and OFF by default — a normal
# renderer build never pays for them. Enable per-build, e.g. -DUSE_GDCM=ON.
#   USE_GDCM : DICOM parsing for the volume_io module — this is the Tier 3-A dep.
#   USE_ITK  : full Insight Toolkit (bundles its own GDCM); not used yet.
#   USE_VTK  : Visualization Toolkit; reference/benchmark only, never linked.
# ----------------------------------------------------------------------------
option(USE_GDCM "Build GDCM (DICOM parsing) for the volume_io module" OFF)
option(USE_ITK  "Build ITK (medical image processing; bundles GDCM)"  OFF)
option(USE_VTK  "Build VTK (visualization toolkit; reference only)"   OFF)

# Learning prototypes (fibers, etc.) — small standalone demos, native only.
option(BUILD_PROTOTYPES "Build learning prototypes under apps/*_prototype and apps/dawn_probe" OFF)

# Compatibility switch for Makefile/CI. Core targets are always registered so
# apps and modules can link them; this option keeps the public CMake API stable.
option(BUILD_CORE "Build the reusable core engine library modules" OFF)
option(BUILD_CORE_EXAMPLES "Build core study examples/tests/benches under apps/core_demos" OFF)
option(BUILD_VOLUME_DEMOS "Build volume module smoke tests under apps/volume_demos" ON)

set(DEFAULT_BUILD_OPENGL_RENDERER_MODULE OFF)
if(BUILD_RENDERER AND NOT USE_WEBGPU)
    set(DEFAULT_BUILD_OPENGL_RENDERER_MODULE ON)
endif()
option(BUILD_OPENGL_RENDERER_MODULE "Build the OpenGL/GLES renderer module" ${DEFAULT_BUILD_OPENGL_RENDERER_MODULE})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
link_directories(${CMAKE_BINARY_DIR}/libs)

message(STATUS "Build output: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Load GLM (header-only math library) for renderer, core, and modules.
include(external/glm.cmake)

if(USE_ITK)
    include(external/itk.cmake)
endif()
if(USE_VTK)
    include(external/vtk.cmake)
endif()

add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(apps)

# ----------------------------------------------------------------------------
# Third-party license disclaimer (brought over from master). Collects the
# license of every CPM dependency into build/third_party.txt — useful for the
# dissertation's attribution. Runs at configure time over ${CPM_PACKAGES}.
# ----------------------------------------------------------------------------
CPMAddPackage(
    NAME CPMLicenses.cmake
    GITHUB_REPOSITORY cpm-cmake/CPMLicenses.cmake
    VERSION 0.0.5
)
write_license_disclaimer("${CMAKE_BINARY_DIR}/third_party.txt" "${CPM_PACKAGES}")
