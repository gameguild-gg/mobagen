# ============================================================================
# WebGPU via Dawn (native) + emdawnwebgpu (Emscripten / Web)
#
# Provides one interface target:
#
# dawn::webgpu - Native (macOS/Win/Linux): links Dawn (webgpu_dawn) and exposes <webgpu/webgpu.h> +
# <webgpu/webgpu_cpp.h>. - Emscripten: pulls in the emdawnwebgpu port from the Dawn tree (replaces
# the deprecated `-sUSE_WEBGPU=1`) via `--use-port=...`.
#
# Notes: * Both Dawn and emdawnwebgpu publish the same `<webgpu/webgpu_cpp.h>` API, so ImGui's
# `imgui_impl_wgpu.cpp` compiles with the Dawn flavor on both. * Building Dawn from source on native
# takes a while (~5-10 min cold).
# ============================================================================

add_library(dawn_webgpu INTERFACE)
add_library(dawn::webgpu ALIAS dawn_webgpu)

# ---------------------------------------------------------------------------
# Dawn requires Python 3 (interpreter) plus the jinja2 module at configure and build time — it
# fetches third-party dependencies and generates headers (webgpu.h / webgpu_cpp.h / tint) from
# templates. Check this BEFORE CPMAddPackage so students fail fast with actionable instructions
# instead of Dawn's mid-configure "find_package(Python3 REQUIRED)" or "Missing dependencies for
# code generation" errors.
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(NOT Python3_FOUND)
    if(WIN32)
      set(_DAWN_PYTHON_HINT
          [[Install Python 3 from https://www.python.org/downloads/ or via 'winget install -e --id Python.Python.3.13' (tick 'Add python.exe to PATH' in the installer), then delete the build directory and reload the CMake project.]]
      )
    elseif(APPLE)
      set(_DAWN_PYTHON_HINT
          [[Install Python 3 via 'brew install python' or from https://www.python.org/downloads/, then delete the build directory and reload the CMake project.]]
      )
    else()
      set(_DAWN_PYTHON_HINT
          [[Install Python 3 via your package manager, e.g. 'sudo apt install python3 python3-pip', then delete the build directory and reload the CMake project.]]
      )
    endif()
    message(
      FATAL_ERROR
        "MoBaGEn requires Python 3 to build Dawn (WebGPU): it drives dependency fetching and "
        "header/code generation. No usable Python 3 interpreter was found on this system. "
        "${_DAWN_PYTHON_HINT}"
    )
  endif()

#  execute_process(
#    COMMAND "${Python3_EXECUTABLE}" -c "import jinja2"
#    RESULT_VARIABLE _DAWN_JINJA2_RESULT
#    OUTPUT_QUIET ERROR_QUIET
#  )
#  if(NOT _DAWN_JINJA2_RESULT EQUAL 0)
#    message(
#      FATAL_ERROR
#        "MoBaGEn requires the python 'jinja2' module to build Dawn (WebGPU): it is used by Dawn's "
#        "code generators. It is missing for interpreter '${Python3_EXECUTABLE}'. Fix with: "
#        "'${Python3_EXECUTABLE}' -m pip install jinja2, then delete the build directory and reload "
#        "the CMake project."
#    )
#  endif()
endif()

# ---------------------------------------------------------------------------
# Native-only build options. Must be set BEFORE CPMAddPackage so Dawn picks them up. On Emscripten
# we use DOWNLOAD_ONLY and skip these entirely.
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
  set(DAWN_FETCH_DEPENDENCIES
      ON
      CACHE BOOL "" FORCE
  )
  set(DAWN_ENABLE_INSTALL
      OFF
      CACHE BOOL "" FORCE
  )
  set(DAWN_BUILD_SAMPLES
      OFF
      CACHE BOOL "" FORCE
  )
  # These are also passed to the nested CPM project via OPTIONS below; we set them in the parent too
  # so any direct introspection (e.g. a message(STATUS ...) that reads the cache) sees the right
  # values.
  set(DAWN_BUILD_MONOLITHIC_LIBRARY
      "STATIC"
      CACHE STRING "" FORCE
  )
  set(DAWN_USE_GLFW
      OFF
      CACHE BOOL "" FORCE
  )
  set(DAWN_USE_WAYLAND
      OFF
      CACHE BOOL "" FORCE
  )
  if(APPLE
     OR WIN32
     OR ANDROID
  )
    set(DAWN_USE_X11
        OFF
        CACHE BOOL "" FORCE
    )
  else()
    set(DAWN_USE_X11
        ON
        CACHE BOOL "" FORCE
    )
  endif()
  set(TINT_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE
  )
  set(TINT_BUILD_CMD_TOOLS
      OFF
      CACHE BOOL "" FORCE
  )
  set(TINT_BUILD_DOCS
      OFF
      CACHE BOOL "" FORCE
  )
  set(DAWN_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE
  )

  # Build only the backend we need per-platform.
  if(APPLE)
    set(DAWN_ENABLE_METAL
        ON
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_VULKAN
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_D3D12
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_DESKTOP_GL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_OPENGLES
        OFF
        CACHE BOOL "" FORCE
    )
  elseif(WIN32)
    set(DAWN_ENABLE_D3D12
        ON
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_VULKAN
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_METAL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_DESKTOP_GL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_OPENGLES
        OFF
        CACHE BOOL "" FORCE
    )
  elseif(ANDROID)
    # Android uses Vulkan exclusively. No X11/Wayland display server.
    set(DAWN_ENABLE_VULKAN
        ON
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_METAL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_D3D12
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_DESKTOP_GL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_OPENGLES
        OFF
        CACHE BOOL "" FORCE
    )
  else() # Linux/BSD
    set(DAWN_ENABLE_VULKAN
        ON
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_METAL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_D3D12
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_DESKTOP_GL
        OFF
        CACHE BOOL "" FORCE
    )
    set(DAWN_ENABLE_OPENGLES
        OFF
        CACHE BOOL "" FORCE
    )
  endif()
endif()

# ---------------------------------------------------------------------------
# Single CPMAddPackage for both native and web. On Emscripten we only need the source tree (for the
# emdawnwebgpu port), so we skip building.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  set(_DAWN_DOWNLOAD_ONLY YES)
else()
  set(_DAWN_DOWNLOAD_ONLY NO)
endif()

string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME dawn
  VERSION 20260423.175430
  URL https://github.com/google/dawn/archive/refs/tags/v20260423.175430.tar.gz
  DOWNLOAD_ONLY ${_DAWN_DOWNLOAD_ONLY}
  OPTIONS "DAWN_BUILD_MONOLITHIC_LIBRARY ${DAWN_BUILD_MONOLITHIC_LIBRARY}"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTADAWN "${AFTER} - ${BEFORE}")
message(STATUS "Dawn fetch/configure TIME: ${DELTADAWN}s")

# ---------------------------------------------------------------------------
# Wire the interface target according to platform.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  # Emscripten 4.0.10+ ships a built-in "remote port" named simply `emdawnwebgpu`. Using it is the
  # recommended path — it tells emcc/em++ to download a pinned, pre-built emdawnwebgpu package and
  # wire it up automatically. Avoids having to build Dawn's CMake target for the web target (we set
  # DOWNLOAD_ONLY above, which would never produce the emdawnwebgpu_pkg that the Dawn source's port
  # file requires).
  set(_EMDAWN_PORT "emdawnwebgpu")
  message(STATUS "Using emdawnwebgpu remote port (built into Emscripten 4.0.10+)")

  # The port replaces the old -sUSE_WEBGPU=1 path; emdawnwebgpu requires Asyncify.
  target_compile_options(dawn_webgpu INTERFACE "SHELL:--use-port=${_EMDAWN_PORT}")
  target_link_options(dawn_webgpu INTERFACE "SHELL:--use-port=${_EMDAWN_PORT}" "SHELL:-sASYNCIFY")
else()
  # Dawn publishes generated headers as INTERFACE_SOURCES on some targets. With cross-directory
  # linking (our root project links through dawn::webgpu), CMake can validate those files before
  # Dawn's generator emits them. Keep include dirs/defines, but drop INTERFACE_SOURCES propagation.
  foreach(_dawn_tgt IN ITEMS dawn_native dawn_proc webgpu_dawn)
    if(TARGET ${_dawn_tgt})
      set_property(TARGET ${_dawn_tgt} PROPERTY INTERFACE_SOURCES "")
    endif()
  endforeach()

  # With the Xcode generator, add_library(STATIC $<TARGET_OBJECTS:...>) creates a target with only a
  # "Frameworks" build phase and no "Sources" phase, so the static library archive (.a) is never
  # produced.  Work around this by creating a custom target that invokes a helper CMake script at
  # build time to merge the Dawn object archives into libwebgpu_dawn.a using libtool. Each Xcode
  # configuration is handled by one add_custom_target variant.
  if(IOS
     AND TARGET webgpu_dawn
     AND TARGET webgpu_dawn_objects
     AND TARGET dawn_native_objects
  )
    # Detect simulator vs device platform from CMAKE_OSX_SYSROOT (set to "iphonesimulator" or full
    # SDK path containing "Simulator").
    if(CMAKE_OSX_SYSROOT MATCHES "simulator")
      set(_IOS_PLATFORM "iphonesimulator")
    else()
      set(_IOS_PLATFORM "iphoneos")
    endif()
    set(_ios_merge_script "${CMAKE_SOURCE_DIR}/cmake/ios_merge_webgpu_dawn.cmake")
    foreach(_cfg Debug Release MinSizeRel RelWithDebInfo)
      add_custom_target(
        webgpu_dawn_merge_${_cfg} ALL
        COMMAND
          "${CMAKE_COMMAND}" -DBUILD_DIR="${CMAKE_BINARY_DIR}"
          -DOUTPUT_DIR="${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}" "-DCONFIG=${_cfg}"
          "-DPLATFORM=${_IOS_PLATFORM}" -P "${_ios_merge_script}"
        BYPRODUCTS "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/${_cfg}/libwebgpu_dawn.a"
        COMMENT "Merging Dawn archives into libwebgpu_dawn.a (${_cfg})"
      )
      add_dependencies(webgpu_dawn_merge_${_cfg} webgpu_dawn_objects dawn_native_objects)
    endforeach()
    add_dependencies(
      dawn_webgpu webgpu_dawn_merge_Debug webgpu_dawn_merge_Release webgpu_dawn_merge_MinSizeRel
      webgpu_dawn_merge_RelWithDebInfo
    )
  endif()

  # Prefer Dawn's convenience monolithic target when available. On some Xcode+iOS generator
  # combinations the bundled archive target `webgpu_dawn` is declared but not emitted as a concrete
  # .a file. In that case, fall back to the granular Dawn targets which are reliably produced and
  # sufficient for our direct C API usage.
  if(IOS)
    # Prefer Dawn's monolithic target on iOS because some Dawn granular targets can propagate
    # generated headers through interface metadata before the generator creates them.
    if(TARGET webgpu_dawn)
      message(STATUS "Dawn iOS link mode: webgpu_dawn")
      target_link_libraries(dawn_webgpu INTERFACE webgpu_dawn)
    elseif(TARGET dawn_native AND TARGET dawn_proc)
      message(STATUS "Dawn iOS link mode: dawn_native + dawn_proc")
      target_link_libraries(dawn_webgpu INTERFACE dawn_native dawn_proc)
    elseif(TARGET dawn::dawn_native AND TARGET dawn::dawn_proc)
      message(STATUS "Dawn iOS link mode: dawn::dawn_native + dawn::dawn_proc")
      target_link_libraries(dawn_webgpu INTERFACE dawn::dawn_native dawn::dawn_proc)
    else()
      message(
        FATAL_ERROR
          "iOS build requires webgpu_dawn or dawn_native + dawn_proc targets, but none were created."
      )
    endif()
  elseif(TARGET webgpu_dawn)
    target_link_libraries(dawn_webgpu INTERFACE webgpu_dawn)
  elseif(TARGET dawn_native AND TARGET dawn_proc)
    message(
      WARNING "Dawn target `webgpu_dawn` not available; falling back to dawn_native + dawn_proc."
    )
    target_link_libraries(dawn_webgpu INTERFACE dawn_native dawn_proc)
  elseif(TARGET dawn::dawn_native AND TARGET dawn::dawn_proc)
    message(
      WARNING
        "Dawn target `webgpu_dawn` not available; falling back to dawn::dawn_native + dawn::dawn_proc."
    )
    target_link_libraries(dawn_webgpu INTERFACE dawn::dawn_native dawn::dawn_proc)
  else()
    message(
      FATAL_ERROR
        "Dawn was added but neither `webgpu_dawn` nor dawn_native+dawn_proc targets were created. "
        "Check the Dawn tag / options pinned in external/dawn.cmake."
    )
  endif()

  target_include_directories(
    dawn_webgpu INTERFACE "${dawn_SOURCE_DIR}/include" "${dawn_BINARY_DIR}/gen/include"
  )
  target_compile_definitions(dawn_webgpu INTERFACE WEBGPU_BACKEND_DAWN=1)
endif()

# ---------------------------------------------------------------------------
# Windows: deploy d3dcompiler_47.dll next to the binaries.
#
# Dawn's D3D backends LoadLibrary("d3dcompiler_47.dll") (FXC shader compiler) at device creation,
# probing the executable directory first. If the DLL is not found there, Dawn falls back to a bare
# LoadLibraryEx call with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, which Windows rejects with
# ERROR_INVALID_PARAMETER (87) for relative filenames — observed on Windows 11 ARM64 (Parallels)
# even though System32 contains a matching ARM64 FXC. Shipping the SDK-redist DLL beside the apps
# sidesteps the loader quirk and keeps fresh machines working without manual DLL copying.
# ---------------------------------------------------------------------------
if(WIN32 AND NOT EMSCRIPTEN)
  # Map the target machine to the Windows SDK redist folder name. ARM64EC maps to arm64 (no EC
  # redist exists; the ARM64 binary is the best available match).
  set(_dawn_fxc_arch "")
  if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|AARCH64)")
    set(_dawn_fxc_arch arm64)
  elseif(CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR CMAKE_SYSTEM_PROCESSOR MATCHES
                                                    "^(AMD64|x86_64|X64)"
  )
    set(_dawn_fxc_arch x64)
  elseif(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_SYSTEM_PROCESSOR MATCHES
                                                      "^(X86|i[3-6]86)"
  )
    set(_dawn_fxc_arch x86)
  endif()

  set(_dawn_fxc_dll "")
  if(_dawn_fxc_arch)
    get_filename_component(
      _dawn_kits_root
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
      ABSOLUTE
    )
    foreach(
      _dawn_sdk_root IN
      ITEMS "$ENV{WindowsSdkDir}" "${_dawn_kits_root}" "C:/Program Files (x86)/Windows Kits/10"
            "C:/Program Files (x86)/Windows Kits/8.1"
    )
      if(EXISTS "${_dawn_sdk_root}")
        file(GLOB _dawn_fxc_hits
             "${_dawn_sdk_root}/Redist/D3D/${_dawn_fxc_arch}/d3dcompiler_47.dll"
        )
        if(_dawn_fxc_hits)
          list(GET _dawn_fxc_hits 0 _dawn_fxc_dll)
          break()
        endif()
      endif()
    endforeach()
  endif()

  if(_dawn_fxc_dll)
    # Copy at configure time so the DLL is present even when IDEs build a single target (the ALL
    # custom target below is not in any specific executable's dependency graph). Re-copies on every
    # configure, which is cheap and keeps fresh machines and cleaned bin/ directories working.
    file(COPY "${_dawn_fxc_dll}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    message(STATUS "Dawn: deployed ${_dawn_fxc_dll} to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    add_custom_target(
      dawn_deploy_d3dcompiler ALL
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dawn_fxc_dll}"
              "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      DEPENDS "${_dawn_fxc_dll}"
      COMMENT
        "Deploying d3dcompiler_47.dll (${_dawn_fxc_arch}) to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      VERBATIM
    )
  else()
    message(
      WARNING
        "d3dcompiler_47.dll not found in the Windows SDK redist (arch '${_dawn_fxc_arch}'). Dawn's "
        "D3D backends need it at runtime — install a Windows SDK with the D3D redist component."
    )
  endif()
endif()
