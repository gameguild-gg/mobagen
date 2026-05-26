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

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
link_directories(${CMAKE_BINARY_DIR}/libs)

# Engine abstraction layer sources
set(ENGINE_SOURCES
    src/main.cpp
    src/engine/shader_program.cpp
    src/engine/vertex_buffer.cpp
    src/engine/vertex_array.cpp
    src/engine/renderer.cpp
    src/engine/engine_c.cpp
)

add_executable(dicom_renderer ${ENGINE_SOURCES})
target_include_directories(dicom_renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Load GLM (header-only math library) for all builds
include(external/glm.cmake)
target_link_libraries(dicom_renderer PRIVATE glm)

# WebGPU option (parallel implementation with OpenGL)
option(USE_WEBGPU "Use WebGPU instead of OpenGL" OFF)
if(USE_WEBGPU)
    target_compile_definitions(dicom_renderer PRIVATE USE_WEBGPU)
    message(STATUS "Building with WebGPU (modern deferred rendering)")
else()
    message(STATUS "Building with OpenGL (immediate-mode rendering)")
endif()

if(EMSCRIPTEN)
    target_compile_options(dicom_renderer PRIVATE -O3)
    target_link_options(dicom_renderer PRIVATE
        -O3
        -sUSE_SDL=2
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sASSERTIONS=1
        -sALLOW_MEMORY_GROWTH=1
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/html/shell.html
    )
    set_target_properties(dicom_renderer PROPERTIES SUFFIX ".html")
else()
    include(external/external.cmake)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(dicom_renderer PRIVATE GLEW_STATIC)
    target_link_libraries(dicom_renderer PRIVATE SDL2-static OpenGL::GL libglew_static)
endif()
