# mobagen_add_module_aot_stage — post-build wamrc AOT stage (todo 20).
#
# mobagen_add_module_aot_stage(target WASM <plugin.wasm> MANIFEST_TOOL <exe> [REQUIRED])
#   → plugin.wasm → plugin.aot (host arch) + plugin.aarch64.aot (cross), then
#     regenerates module.manifest so the toolchain stanza (producer wamrc,
#     version = the WAMR pin) and the plugin.aot payload hash are stamped
#     (T2's manifest schema; the loader of todo 7 enforces the version).
#   REQUIRED: fatal when no pinned wamrc resolves (packaging, todo 21); the
#     default is a loud skip so machines without the cache still configure.
#
# Platform gate:
#   desktop/android  → stage runs (host arch + aarch64 cross)
#   iOS toolchain    → no-op with STATUS message; an explicit request via
#                      -DMOBAGEN_MODULE_AOT_FORCE=1 FATAL_ERRORs at configure
#                      time (mirrors the WAMR_BUILD_AOT guard style of
#                      core/sources/plugins/wamr/CMakeLists.txt).
#   EMSCRIPTEN (web) → no-op with STATUS message (no .aot may ship for web).
#
# wamrc provenance (never a system wamrc):
#   1. MOBAGEN_WAMRC_EXECUTABLE cache variable (explicit override)
#   2. external/wamrc-cache/wamrc/wamrc (T7's prebuilt 2.4.5, gitignored)
#   3. source build from the SAME pinned WAMR-2.4.5 CPM source — needs a
#      prebuilt LLVM (wamr-compiler's own CMakeLists requires one); scripted
#      in cmake/build_wamrc.cmake, run via:
#        cmake -DMOBAGEN_WAMRC_SOURCE_DIR=<wamr source> \
#              -DMOBAGEN_WAMRC_OUTPUT_DIR=<install dir> -P cmake/build_wamrc.cmake
#      then set -DMOBAGEN_WAMRC_EXECUTABLE=<install dir>/wamrc.
#   The resolved binary's `--version` output must report the pinned version
#   (checked at configure time when the stage is active).
#
# wamrc 2.4.5 flag set (from `wamrc --help` + a live run on the cached binary):
#   --target=x86_64|aarch64  explicit per-arch compilation
#   --size-level=1 (x86_64)  medium code model — wamrc auto-selects 1 only on
#                            mac/windows hosts; pinned explicitly so a linux
#                            build host emits the same (relocation-safe) model.
#   --size-level=3 (aarch64) small code model — REQUIRED: LLVM aborts aarch64
#                            compilation with "Only small, tiny and large code
#                            models are allowed on AArch64" at size-level 1
#                            (medium). This is the WAMR issues #242/#3164
#                            aarch64 code-model caveat; small is the only
#                            portable choice and also wamrc's aarch64 default.
#   NO 16KB-page option exists in wamrc 2.4.5: the AOT loader aligns sections
#   with os_getpagesize() ON DEVICE at load time (aot_loader.c), so .aot files
#   are page-size agnostic. Android 16KB compat is a native WAMR-lib linker
#   concern (-Wl,-z,max-page-size=16384, todo 21 packaging), NOT a wamrc flag.
#
# Usage (tools/modules/CMakeLists.txt, module guest targets):
#   include(mobagen_module_aot)  # once, from a directory on CMAKE_MODULE_PATH
#   mobagen_add_module_aot_stage(<guest-target>
#     WASM <plugin.wasm output path>        # produced by an add_custom_command
#     MANIFEST_TOOL $<TARGET_FILE:MobagenModuleManifest>  # extraction CLI
#   )

set(MOBAGEN_WAMRC_VERSION "2.4.5" CACHE STRING "Required wamrc version (the WAMR pin)")

function(mobagen_module_aot_platform_gate out_enabled)
  # Desktop/android run the stage; iOS/web are no-ops (never produce .aot).
  set(enabled TRUE)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(enabled FALSE)
  elseif(EMSCRIPTEN)
    set(enabled FALSE)
  endif()
  set(${out_enabled} ${enabled} PARENT_SCOPE)
endfunction()

function(_mobagen_wamrc_resolve out_wamrc)
  # Provenance chain: explicit cache var → T7 prebuilt cache → (documented)
  # source build via cmake/build_wamrc.cmake. NEVER falls back to PATH.
  # Returns empty + STATUS when nothing resolves (caller decides whether that
  # is fatal via the REQUIRED keyword).
  if(MOBAGEN_WAMRC_EXECUTABLE)
    if(NOT EXISTS "${MOBAGEN_WAMRC_EXECUTABLE}")
      message(FATAL_ERROR "MOBAGEN_WAMRC_EXECUTABLE='${MOBAGEN_WAMRC_EXECUTABLE}' does not exist")
    endif()
    set(${out_wamrc} "${MOBAGEN_WAMRC_EXECUTABLE}" PARENT_SCOPE)
    return()
  endif()
  set(_cache "${CMAKE_SOURCE_DIR}/external/wamrc-cache/wamrc/wamrc")
  if(EXISTS "${_cache}")
    set(${out_wamrc} "${_cache}" PARENT_SCOPE)
    return()
  endif()
  set(${out_wamrc} "" PARENT_SCOPE)
  message(STATUS
    "mobagen module AOT stage: no pinned wamrc found (expected external/wamrc-cache/wamrc/wamrc or "
    "-DMOBAGEN_WAMRC_EXECUTABLE). Source build: cmake -DMOBAGEN_WAMRC_SOURCE_DIR=<wasm-micro-runtime source> "
    "-DMOBAGEN_WAMRC_OUTPUT_DIR=<install dir> -P cmake/build_wamrc.cmake. A system wamrc is never used.")
endfunction()

function(mobagen_add_module_aot_stage target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "mobagen_add_module_aot_stage: '${target}' is not a target")
  endif()

  cmake_parse_arguments(MOBAGEN_AOT "REQUIRED" "WASM;MANIFEST_TOOL" "" ${ARGN})
  if(NOT MOBAGEN_AOT_WASM)
    message(FATAL_ERROR "mobagen_add_module_aot_stage(${target}): WASM <plugin.wasm path> is required")
  endif()
  if(NOT MOBAGEN_AOT_MANIFEST_TOOL)
    message(FATAL_ERROR "mobagen_add_module_aot_stage(${target}): MANIFEST_TOOL <MobagenModuleManifest path> is required")
  endif()

  # iOS guard, T4 style: the cache var exists only on an explicit -D request;
  # iOS stays interpreter-only, so requesting the stage there is fatal.
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND MOBAGEN_MODULE_AOT_FORCE)
    message(FATAL_ERROR
      "MOBAGEN_MODULE_AOT_FORCE=1 was requested but the iOS toolchain is interpreter-only "
      "(no AOT on iOS, per mobagen policy). Reconfigure without the force flag.")
  endif()

  mobagen_module_aot_platform_gate(_aot_enabled)
  if(NOT _aot_enabled)
    message(STATUS
      "mobagen module AOT stage: '${target}' skipped — platform '${CMAKE_SYSTEM_NAME}'"
      " (EMSCRIPTEN=${EMSCRIPTEN}) ships interpreter-only, no plugin.aot")
    return()
  endif()

  _mobagen_wamrc_resolve(_wamrc)
  if(NOT _wamrc)
    # Packaging targets (todo 21) pass REQUIRED: an AOT-flagged package must
    # never silently degrade to interpreter-only.
    if(MOBAGEN_AOT_REQUIRED)
      message(FATAL_ERROR
        "mobagen module AOT stage: '${target}' requires wamrc ${MOBAGEN_WAMRC_VERSION} from the pinned source "
        "but none resolved. Populate external/wamrc-cache/ or set MOBAGEN_WAMRC_EXECUTABLE.")
    endif()
    message(STATUS "mobagen module AOT stage: '${target}' skipped (no pinned wamrc available)")
    return()
  endif()

  get_filename_component(_wasm_dir "${MOBAGEN_AOT_WASM}" DIRECTORY)
  set(_aot_host "${_wasm_dir}/plugin.aot")
  set(_aot_aarch64 "${_wasm_dir}/plugin.aarch64.aot")
  set(_manifest "${_wasm_dir}/module.manifest")

  # Configure-time version pin check (cheap, runs once per configure).
  execute_process(
    COMMAND "${_wamrc}" --version
    OUTPUT_VARIABLE _wamrc_version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _wamrc_version_status
  )
  if(NOT _wamrc_version_status EQUAL 0)
    message(FATAL_ERROR "mobagen module AOT stage: '${_wamrc}' --version failed (status ${_wamrc_version_status})")
  endif()
  if(NOT _wamrc_version_output MATCHES "${MOBAGEN_WAMRC_VERSION}")
    message(FATAL_ERROR
      "mobagen module AOT stage: '${_wamrc}' reports '${_wamrc_version_output}' but the pin is wamrc ${MOBAGEN_WAMRC_VERSION}; "
      "refusing to use a mismatched compiler")
  endif()

  # Host-arch AOT: --target resolved at build time from the configured
  # CMAKE_SYSTEM_PROCESSOR so cross-configured desktop/android builds compile
  # for the device, not the build host. size-level: x86_64 pins the medium
  # code model (1); aarch64 only allows small (3) — see flag notes above.
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|AARCH64)$")
    set(_host_target aarch64)
    set(_host_size_level 3)
  else()
    set(_host_target x86_64)
    set(_host_size_level 1)
  endif()

  add_custom_command(
    OUTPUT "${_aot_host}"
    COMMAND "${_wamrc}" --target=${_host_target} --size-level=${_host_size_level} -o "${_aot_host}" "${MOBAGEN_AOT_WASM}"
    DEPENDS "${MOBAGEN_AOT_WASM}"
    COMMENT "wamrc AOT (${_host_target}) for ${target}"
    VERBATIM
  )
  # aarch64 cross variant (android arm64-v8a / arm64 desktops). Cross output
  # is auxiliary (suffixed) so it never collides with the runtime-preferred
  # plugin.aot of a different-arch host.
  add_custom_command(
    OUTPUT "${_aot_aarch64}"
    COMMAND "${_wamrc}" --target=aarch64 --size-level=3 -o "${_aot_aarch64}" "${MOBAGEN_AOT_WASM}"
    DEPENDS "${MOBAGEN_AOT_WASM}"
    COMMENT "wamrc AOT (aarch64 cross) for ${target}"
    VERBATIM
  )
  # Manifest stamp: extraction CLI regenerates module.manifest from the wasm
  # annotation table + hashes both payloads + records the wamrc toolchain.
  add_custom_command(
    OUTPUT "${_manifest}"
    COMMAND ${MOBAGEN_AOT_MANIFEST_TOOL} manifest --aot "${_aot_host}" --toolchain-producer wamrc
            --toolchain-version "${MOBAGEN_WAMRC_VERSION}" "${MOBAGEN_AOT_WASM}" "${_manifest}"
    DEPENDS "${MOBAGEN_AOT_WASM}" "${_aot_host}" ${MOBAGEN_AOT_MANIFEST_TOOL}
    COMMENT "module.manifest (wamrc ${MOBAGEN_WAMRC_VERSION} stamped) for ${target}"
    VERBATIM
  )
  add_custom_target(${target}_aot_stage DEPENDS "${_aot_host}" "${_aot_aarch64}" "${_manifest}")
  add_dependencies(${target} ${target}_aot_stage)
  message(STATUS "mobagen module AOT stage: '${target}' wamrc=${_wamrc} host=${_host_target} (+aarch64 cross)")
endfunction()
