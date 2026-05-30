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
        # Generated below: the WGSL sources are substituted into the shell.
        set(SHELL_FILE "${CMAKE_BINARY_DIR}/generated/shell_webgpu.html")
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

# ----------------------------------------------------------------------------
# Shaders — both GLSL and WGSL are EMBEDDED at build time (no runtime fetch).
# shaders/ is the source of truth; one file per program holds both stages.
# Editing any shader re-runs CMake via CMAKE_CONFIGURE_DEPENDS.
#
# GLSL (C++ side): baked into a generated header (build/generated/embedded_shaders.h),
#   compiled twice with VERTEX_SHADER / FRAGMENT_SHADER.
# WGSL (JS side): substituted into the WebGPU shell at configure time
#   (build/generated/shell_webgpu.html), so the shell carries the WGSL inline.
# ----------------------------------------------------------------------------
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(GLSL_SHADERS raygen.glsl blit.glsl)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
set(GEN_HEADER ${GEN_DIR}/embedded_shaders.h)

set(_gen "#pragma once\n// AUTO-GENERATED from shaders/*.glsl. Edit those, not this.\nnamespace shaders {\n\n")
foreach(_f ${GLSL_SHADERS})
    file(READ ${SHADER_DIR}/${_f} _content)
    string(MAKE_C_IDENTIFIER ${_f} _id)
    string(TOUPPER ${_id} _id)
    string(APPEND _gen "inline constexpr const char* ${_id} = R\"GLSL(\n${_content})GLSL\";\n\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHADER_DIR}/${_f})
endforeach()
string(APPEND _gen "}  // namespace shaders\n")
file(WRITE ${GEN_HEADER} "${_gen}")

# WGSL: substitute the .wgsl sources into the WebGPU shell template (@ONLY so the
# JS template-literal ${...} and the WGSL @attributes are left untouched).
if(EMSCRIPTEN AND USE_WEBGPU)
    file(READ ${SHADER_DIR}/raygen.wgsl RAYGEN_WGSL)
    file(READ ${SHADER_DIR}/blit.wgsl   BLIT_WGSL)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/html/shell_webgpu.html.in
                   ${GEN_DIR}/shell_webgpu.html @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${SHADER_DIR}/raygen.wgsl
        ${SHADER_DIR}/blit.wgsl
        ${CMAKE_CURRENT_SOURCE_DIR}/html/shell_webgpu.html.in)
endif()

add_executable(dicom_renderer ${ENGINE_SOURCES})
target_include_directories(dicom_renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GEN_DIR}
)

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
