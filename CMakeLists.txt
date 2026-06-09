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
# Renderer selection (compile-time, NOT runtime).
#
# A browser <canvas> can only ever hold one context type, so WebGL and WebGPU
# cannot share a canvas at runtime. Each renderer is therefore its own build:
#   -DUSE_WEBGPU=OFF (default) -> WebGL2 build  -> build/wasm-webgl
#   -DUSE_WEBGPU=ON            -> WebGPU build  -> build/wasm-webgpu
# Declared BEFORE it is used below.
# ----------------------------------------------------------------------------
option(USE_WEBGPU "Build the WebGPU (G3) renderer instead of WebGL2 (G2)" OFF)
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
option(BUILD_PROTOTYPES "Build learning prototypes under prototypes/" OFF)

# Core engine modules (reactive / messaging / ecs / scene / jobs).
#
# BUILD_CORE builds only reusable core targets. BUILD_CORE_EXAMPLES builds the
# study/demo/test executables under examples/core. This keeps core as a library
# layer instead of mixing examples into it.
option(BUILD_CORE "Build the reusable core engine library modules" OFF)
option(BUILD_CORE_EXAMPLES "Build core study examples/tests/benches under examples/core" OFF)

# Shell selection (Emscripten only). Each renderer/build already lives in its own
# binary dir (build/wasm-webgl, build/wasm-webgpu, build/native), so output goes
# straight to ${CMAKE_BINARY_DIR}/{bin,libs} — no per-type nesting (matches
# master's convention; kills the old build/wasm-webgl/wasm-webgl/bin doubling).
if(EMSCRIPTEN)
    if(USE_WEBGPU)
        # Dawn web renders in C++/wasm to #canvas; the shell is just a plain
        # canvas host (the UI is ImGui, drawn in-canvas — no JS controls).
        set(SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/html/shell_dawn.html")
    else()
        set(SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/html/shell_webgl.html")
    endif()
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
link_directories(${CMAKE_BINARY_DIR}/libs)

message(STATUS "Build output: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Load GLM (header-only math library) for renderer and core modules.
include(external/glm.cmake)

if(BUILD_RENDERER)

# ----------------------------------------------------------------------------
# Sources
#
# renderer.cpp        - thin GL render abstraction, used by the WebGL build.
# engine_c.cpp        - C ABI boundary (study module): lets the engine be called
#                       from C/Python/JS. Not yet consumed by a second front-end,
#                       kept compiled so it stays correct as the engine evolves.
# ----------------------------------------------------------------------------
# The WebGPU build (Dawn + ImGui) compiles only main.cpp — the src/engine/*.cpp
# files are OpenGL and have no place in a WebGPU build. The WebGL2 build keeps them.
if(USE_WEBGPU)
    set(ENGINE_SOURCES src/main.cpp)
else()
    set(ENGINE_SOURCES
        src/main.cpp
        src/engine/shader_program.cpp
        src/engine/vertex_buffer.cpp
        src/engine/vertex_array.cpp
        src/engine/texture.cpp
        src/engine/texture3d.cpp
        src/engine/framebuffer.cpp
        src/engine/renderer.cpp
        src/engine/engine_c.cpp
    )
endif()

# ----------------------------------------------------------------------------
# Shaders — both GLSL and WGSL are EMBEDDED at build time (no runtime fetch).
# shaders/ is the source of truth; one file per program holds both stages.
# Editing any shader re-runs CMake via CMAKE_CONFIGURE_DEPENDS.
#
# GLSL + WGSL (C++ side): baked into a generated header
#   (build/generated/embedded_shaders.h). WebGL compiles GLSL; WebGPU compiles
#   WGSL through Dawn. Keeping both in one generated header means shaders stay
#   source-controlled under shaders/ and the executable never fetches them at
#   runtime.
# ----------------------------------------------------------------------------
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(GLSL_SHADERS raygen.glsl blit.glsl)
set(WGSL_SHADERS raygen.wgsl blit.wgsl histogram.wgsl)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
set(GEN_HEADER ${GEN_DIR}/embedded_shaders.h)

set(_gen "#pragma once\n// AUTO-GENERATED from shaders/*.glsl and shaders/*.wgsl. Edit those, not this.\nnamespace shaders {\n\n")
foreach(_f ${GLSL_SHADERS})
    file(READ ${SHADER_DIR}/${_f} _content)
    string(MAKE_C_IDENTIFIER ${_f} _id)
    string(TOUPPER ${_id} _id)
    string(APPEND _gen "inline constexpr const char* ${_id} = R\"GLSL(\n${_content})GLSL\";\n\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHADER_DIR}/${_f})
endforeach()
foreach(_f ${WGSL_SHADERS})
    file(READ ${SHADER_DIR}/${_f} _content)
    string(MAKE_C_IDENTIFIER ${_f} _id)
    string(TOUPPER ${_id} _id)
    string(APPEND _gen "inline constexpr const char* ${_id} = R\"WGSL(\n${_content})WGSL\";\n\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHADER_DIR}/${_f})
endforeach()
string(APPEND _gen "}  // namespace shaders\n")
file(WRITE ${GEN_HEADER} "${_gen}")

add_executable(dicom_renderer ${ENGINE_SOURCES})
target_include_directories(dicom_renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ecs
    ${CMAKE_CURRENT_SOURCE_DIR}/core/scene
    ${CMAKE_CURRENT_SOURCE_DIR}/core/render
    ${GEN_DIR}
)

target_link_libraries(dicom_renderer PRIVATE glm)

if(USE_WEBGPU)
    target_compile_definitions(dicom_renderer PRIVATE USE_WEBGPU)
    # WebGPU host = Dawn (native: D3D12/Metal/Vulkan) or emdawnwebgpu (web), with
    # Dear ImGui via imgui_impl_sdl3 + imgui_impl_wgpu. IMGUI already links SDL3 +
    # mobagen::webgpu transitively; named explicitly for clarity.
    include(external/dawn.cmake)
    include(external/sdl.cmake)
    include(external/imgui.cmake)
    target_link_libraries(dicom_renderer PRIVATE IMGUI mobagen::webgpu SDL3::SDL3-static)
    message(STATUS "Renderer: WebGPU (G3) — Dawn + ImGui")
else()
    message(STATUS "Renderer: WebGL2 (G2) — immediate-mode")
endif()

endif() # BUILD_RENDERER

# ----------------------------------------------------------------------------
# Optional medical-imaging dependencies (see options above). The .cmake files
# are only included when enabled, so the heavy source build is skipped for a
# normal renderer build. GDCM is wired up for the volume_io module (Tier 3-A):
# when -DUSE_GDCM=ON, volume_io.cpp gets HAVE_GDCM and links the reader libs.
# ----------------------------------------------------------------------------
if(USE_GDCM)
    include(external/gdcm.cmake)
    if(BUILD_RENDERER)
        target_compile_definitions(dicom_renderer PRIVATE HAVE_GDCM)
        target_sources(dicom_renderer PRIVATE src/volume_io/volume_io.cpp)
        if(NOT EMSCRIPTEN)
            target_compile_definitions(dicom_renderer PRIVATE
                MOBAGEN_DICOM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/dicom")
        endif()
        target_link_libraries(dicom_renderer PRIVATE
            gdcmMSFF gdcmDSED gdcmDICT gdcmIOD gdcmCommon)
    endif()
endif()
if(USE_ITK)
    include(external/itk.cmake)
    if(BUILD_RENDERER)
        target_compile_definitions(dicom_renderer PRIVATE HAVE_ITK)
    endif()
endif()
if(USE_VTK)
    include(external/vtk.cmake)
    if(BUILD_RENDERER)
        target_compile_definitions(dicom_renderer PRIVATE HAVE_VTK)
    endif()
endif()

# Standalone loader smoke test (native + GDCM only): a tiny console exe that
# loads a DICOM series and prints its metadata, independent of the renderer.
# Build: cmake --build build/native --target volume_io_test
if(USE_GDCM AND NOT EMSCRIPTEN)
    add_executable(volume_io_test
        src/volume_io/volume_io_test.cpp
        src/volume_io/volume_io.cpp)
    target_include_directories(volume_io_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(volume_io_test PRIVATE HAVE_GDCM)
    target_link_libraries(volume_io_test PRIVATE gdcmMSFF gdcmDSED gdcmDICT gdcmIOD gdcmCommon)
endif()

if(NOT EMSCRIPTEN)
    add_executable(volume_buffer_test src/volume/volume_buffer_test.cpp)
    target_include_directories(volume_buffer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(volume_buffer_test PRIVATE glm)
endif()

# Learning prototypes (native only — the stackful fiber demo uses Win32 fibers).
if(BUILD_PROTOTYPES AND NOT EMSCRIPTEN)
    add_subdirectory(prototypes/fibers)
endif()

# Core library targets and their optional examples. Examples intentionally live
# outside core/; core/ should be safe to link from renderer/editor/wasm modules
# without creating any executable study programs.
if(BUILD_CORE OR BUILD_CORE_EXAMPLES)
    add_subdirectory(core)
endif()
if(BUILD_CORE_EXAMPLES)
    add_subdirectory(examples)
endif()

set(VOLUME_RAW ${CMAKE_CURRENT_SOURCE_DIR}/assets/volume.raw)
set(VOLUME_MVOL ${CMAKE_CURRENT_SOURCE_DIR}/assets/volume.mvol)

if(BUILD_RENDERER)

if(EMSCRIPTEN)
    message(STATUS "Shell file: ${SHELL_FILE}")
    # Emscripten injects the generated JS/WASM loader into --shell-file at link
    # time. CMake does not infer that dependency from a linker flag, so teach the
    # target that editing html/shell_*.html must relink dicom_renderer.html.
    set_property(TARGET dicom_renderer APPEND PROPERTY LINK_DEPENDS "${SHELL_FILE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SHELL_FILE}")
    target_compile_options(dicom_renderer PRIVATE -O3)
    if(USE_WEBGPU)
        # Dawn web (emdawnwebgpu): the WebGPU port + ASYNCIFY arrive via
        # mobagen::webgpu (external/dawn.cmake); SDL3 is already linked above.
        # No WebGL flags. -sUSE_SDL=0 keeps emscripten from bundling its own SDL.
        target_link_options(dicom_renderer PRIVATE
            -O3
            -sUSE_SDL=0
            -sASSERTIONS=1
            -sALLOW_MEMORY_GROWTH=1
            --shell-file ${SHELL_FILE}
        )
    else()
        # WebGL2 build: SDL3 from source (-sUSE_SDL=0) + WebGL2 / GLES3.
        include(external/sdl.cmake)
        target_link_libraries(dicom_renderer PRIVATE SDL3::SDL3-static)
        target_link_options(dicom_renderer PRIVATE
            -O3
            -sUSE_SDL=0
            -sMIN_WEBGL_VERSION=2
            -sMAX_WEBGL_VERSION=2
            -sFULL_ES3=1
            -sASSERTIONS=1
            -sALLOW_MEMORY_GROWTH=1
            --shell-file ${SHELL_FILE}
        )
    endif()
    if(USE_WEBGPU)
        # WebGPU (Dawn) reads the offline-converted volume from the WASM FS.
        # `make assets` materializes assets/volume.mvol from assets/assets.json;
        # without it, the renderer falls back to the synthetic phantom.
        if(EXISTS ${VOLUME_MVOL})
            target_link_options(dicom_renderer PRIVATE
                "SHELL:--preload-file ${VOLUME_MVOL}@/volume.mvol")
        else()
            message(WARNING "assets/volume.mvol not found; web WebGPU build will use the phantom. Run: make assets")
        endif()
    else()
        # WebGL reads volume.raw in C++ from the WASM virtual FS — preload it there.
        if(EXISTS ${VOLUME_RAW})
            target_link_options(dicom_renderer PRIVATE
                "SHELL:--preload-file ${VOLUME_RAW}@/volume.raw")
        else()
            message(WARNING "assets/volume.raw not found; web WebGL build will use the synthetic fallback. Run: make assets")
        endif()
    endif()
    set_target_properties(dicom_renderer PROPERTIES SUFFIX ".html")
else()
    # Native build.
    target_compile_definitions(dicom_renderer PRIVATE VOLUME_PATH="${VOLUME_RAW}")
    if(USE_WEBGPU)
        # Dawn native (D3D12/Metal/Vulkan): IMGUI + mobagen::webgpu + SDL3 are
        # already linked above. No OpenGL/GLEW in a WebGPU build.
    else()
        # WebGL2 / OpenGL native build.
        include(external/external.cmake)
        find_package(OpenGL REQUIRED)
        target_compile_definitions(dicom_renderer PRIVATE GLEW_STATIC)
        target_link_libraries(dicom_renderer PRIVATE SDL3::SDL3-static OpenGL::GL libglew_static)
    endif()
endif()

endif() # BUILD_RENDERER

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
