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

# ----------------------------------------------------------------------------
# Renderer selection (compile-time, NOT runtime).
#
# A browser <canvas> can only ever hold one context type, so WebGL and WebGPU
# cannot share a canvas at runtime. Each renderer is therefore its own build:
#   -DUSE_WEBGPU=OFF (default) -> WebGL2 build  -> build/wasm-webgl
#   -DUSE_WEBGPU=ON            -> WebGPU build  -> build/wasm-webgpu
# Declared BEFORE it is used below.
# ----------------------------------------------------------------------------
option(USE_WEBGPU "Build the WebGPU (G3) renderer instead of WebGL2 (G2)" OFF)

# Organize output by build type
if(EMSCRIPTEN)
    if(USE_WEBGPU)
        set(BUILD_TYPE_DIR "wasm-webgpu")
        set(SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/html/shell_webgpu.html")
    else()
        set(BUILD_TYPE_DIR "wasm-webgl")
        set(SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/html/shell_webgl.html")
    endif()
else()
    set(BUILD_TYPE_DIR "native")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BUILD_TYPE_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BUILD_TYPE_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BUILD_TYPE_DIR}/bin)
link_directories(${CMAKE_BINARY_DIR}/${BUILD_TYPE_DIR}/libs)

message(STATUS "Build output: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# ----------------------------------------------------------------------------
# Sources
#
# renderer.cpp        - thin GL render abstraction, used by the WebGL build.
# engine_c.cpp        - C ABI boundary (study module): lets the engine be called
#                       from C/Python/JS. Not yet consumed by a second front-end,
#                       kept compiled so it stays correct as the engine evolves.
# ----------------------------------------------------------------------------
set(ENGINE_SOURCES
    src/main.cpp
    src/engine/shader_program.cpp
    src/engine/vertex_buffer.cpp
    src/engine/vertex_array.cpp
    src/engine/texture.cpp
    src/engine/framebuffer.cpp
    src/engine/renderer.cpp
    src/engine/engine_c.cpp
)

add_executable(dicom_renderer ${ENGINE_SOURCES})
target_include_directories(dicom_renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Load GLM (header-only math library) for all builds
include(external/glm.cmake)
target_link_libraries(dicom_renderer PRIVATE glm)

if(USE_WEBGPU)
    target_compile_definitions(dicom_renderer PRIVATE USE_WEBGPU)
    message(STATUS "Renderer: WebGPU (G3) — deferred-mode, compute-capable")
else()
    message(STATUS "Renderer: WebGL2 (G2) — immediate-mode")
endif()

if(EMSCRIPTEN)
    message(STATUS "Shell file: ${SHELL_FILE}")
    target_compile_options(dicom_renderer PRIVATE -O3)
    target_link_options(dicom_renderer PRIVATE
        -O3
        -sUSE_SDL=2
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sASSERTIONS=1
        -sALLOW_MEMORY_GROWTH=1
        --shell-file ${SHELL_FILE}
    )
    set_target_properties(dicom_renderer PROPERTIES SUFFIX ".html")
else()
    # Native build is WebGL/OpenGL only (no WebGPU path off the web).
    include(external/external.cmake)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(dicom_renderer PRIVATE GLEW_STATIC)
    target_link_libraries(dicom_renderer PRIVATE SDL2-static OpenGL::GL libglew_static)
endif()
