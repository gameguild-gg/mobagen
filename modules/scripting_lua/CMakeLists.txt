# Lua scripting guest (dynamic-loading-all-platforms todo 17).
#
# Lua scripting guest (dynamic-loading-all-platforms todo 17).
#
# Lua 5.4.7 (MIT) fetched via CPM (external/lua.cmake) — the engine lives
# ONLY inside the wasm guest, never embedded natively in the core. Mirrors
# modules/scripting_quickjs/ (todo 16, the structural template): emcc
# standalone guest for the web trees, wasi-sdk guest for WAMR, manifest via
# the extraction tool, AOT stage (todo 20 helper) on desktop/android, and a
# HARD 8 MiB size assert (Chromium main-thread sync-compile budget).
#
# DIVERGENCES FROM QUICKJS:
#  1. Trap-based error recovery (MOBAGEN_LUA_TRAP_RECOVERY patches applied
#     to a copy of the fetched tree — lua-5.4.7-mobagen.patch): wasm
#     import-free builds have no setjmp/longjmp, so a Lua error traps and
#     the host re-enters through mobagen_scripting_error_v1 to finish the
#     unwind + collect the message (see portable_lua_plugin.c).
#  2. Math: wasm32 lowers __builtin_sin/pow/... to libcalls, so math comes
#     from the wasi-sysroot libc.a members (lua_math_archive.cmake) on the
#     wasi path and from emscripten's musl on the emcc path — the shim
#     defines allocation + FILE drop-sinks only.

include(${CMAKE_SOURCE_DIR}/external/lua.cmake)

set(MOBAGEN_LUA_GUEST_WASM_EXPORTS
    -Wl,--export=mobagen_wasm_plugin_allocate_v1
    -Wl,--export=mobagen_wasm_plugin_deallocate_v1
    -Wl,--export=mobagen_wasm_plugin_query_v1
    -Wl,--export=mobagen_wasm_plugin_configure_v1
    -Wl,--export=mobagen_wasm_plugin_start_v1
    -Wl,--export=mobagen_wasm_plugin_quiesce_v1
    -Wl,--export=mobagen_wasm_plugin_stop_v1
    -Wl,--export=mobagen_wasm_plugin_process_v1
    -Wl,--export=mobagen_scripting_eval_v1
    -Wl,--export=mobagen_scripting_call_v1
    -Wl,--export=mobagen_scripting_error_v1
    -Wl,--export=mobagen_module_exports_v1
    -Wl,--export=mobagen_module_entry_v1)

# Patched copy of the fetched tree (CPM cache stays pristine; re-copied +
# re-patched only when the patch or upstream sources change).
set(MOBAGEN_LUA_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/lua-5.4.7-mobagen.patch)
set(MOBAGEN_LUA_PATCHED_SOURCE_DIR ${CMAKE_BINARY_DIR}/mobagen-lua-sources/lua-5.4.7)
set(MOBAGEN_LUA_PATCH_MARKER ${CMAKE_BINARY_DIR}/mobagen-lua-sources/.patched)

file(GLOB MOBAGEN_LUA_FETCHED_SOURCES CONFIGURE_DEPENDS ${lua_SOURCE_DIR}/src/*.c)
add_custom_command(
  OUTPUT ${MOBAGEN_LUA_PATCH_MARKER}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/mobagen-lua-sources
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${lua_SOURCE_DIR} ${MOBAGEN_LUA_PATCHED_SOURCE_DIR}
  COMMAND patch -d ${MOBAGEN_LUA_PATCHED_SOURCE_DIR} -p1 --forward < ${MOBAGEN_LUA_PATCH}
  COMMAND ${CMAKE_COMMAND} -E touch ${MOBAGEN_LUA_PATCH_MARKER}
  DEPENDS ${MOBAGEN_LUA_PATCH} ${MOBAGEN_LUA_FETCHED_SOURCES}
  COMMENT "applying the mobagen freestanding patches to the fetched Lua tree"
  VERBATIM
)
add_custom_target(mobagen_lua_patched_sources DEPENDS ${MOBAGEN_LUA_PATCH_MARKER})

file(GLOB MOBAGEN_LUA_PATCHED_SOURCES CONFIGURE_DEPENDS ${MOBAGEN_LUA_PATCHED_SOURCE_DIR}/src/*.c)
list(FILTER MOBAGEN_LUA_PATCHED_SOURCES EXCLUDE REGEX "src/(lua|luac)\.c$")

set(MOBAGEN_LUA_FREESTANDING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_libc_shim.c
    ${MOBAGEN_LUA_PATCHED_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/portable_lua_plugin.c)

set(MOBAGEN_LUA_FREESTANDING_INCLUDES
    -I${MOBAGEN_LUA_PATCHED_SOURCE_DIR}/src
    -I${CMAKE_SOURCE_DIR}/sdk/include)

# Chromium main-thread sync-compile budget (review rr-dlap-1) — same assert
# as todo 16; the browser sync path THROWS BackendFailure at this limit.
set(MOBAGEN_LUA_MAX_WASM_BYTES 8388608)

function(mobagen_lua_size_check_step target wasm_path)
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -DMOBAGEN_LUA_WASM=${wasm_path} -DMOBAGEN_LUA_MAX_BYTES=${MOBAGEN_LUA_MAX_WASM_BYTES}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/lua_size_check.cmake
    VERBATIM
    COMMENT "Lua guest size assert (< ${MOBAGEN_LUA_MAX_WASM_BYTES} bytes)"
  )
endfunction()

if(EMSCRIPTEN)
  add_executable(MobagenPortableLuaPlugin ${MOBAGEN_LUA_FREESTANDING_SOURCES})
  add_dependencies(MobagenPortableLuaPlugin mobagen_lua_patched_sources)
  target_compile_features(MobagenPortableLuaPlugin PRIVATE c_std_11)
  target_link_libraries(MobagenPortableLuaPlugin PRIVATE mobagen::plugin_sdk)
  target_compile_options(MobagenPortableLuaPlugin PRIVATE -DMOBAGEN_LUA_TRAP_RECOVERY=1)
  target_include_directories(MobagenPortableLuaPlugin PRIVATE
    ${MOBAGEN_LUA_PATCHED_SOURCE_DIR}/src)
  # Capability, not thread usage (todo 18): =0 overrides come LAST because the
  # root CMakeLists appends the shared variant's USE_PTHREADS/SHARED_MEMORY
  # to every target (emcc last-flag-wins).
  target_link_options(
    MobagenPortableLuaPlugin
    PRIVATE
    -sSTANDALONE_WASM=1
    -sALLOW_MEMORY_GROWTH=0
    -sUSE_PTHREADS=0
    -sSHARED_MEMORY=0
    -matomics
    -mbulk-memory
    -Wl,--no-entry
    -Wl,--export-memory
    ${MOBAGEN_LUA_GUEST_WASM_EXPORTS}
  )
  set_target_properties(
    MobagenPortableLuaPlugin
    PROPERTIES OUTPUT_NAME plugin SUFFIX ".wasm"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/mobagen.scripting.lua.plugin"
  )
  # The vendored Lua core compiles freestanding against musl's headers on
  # this path; the trap-recovery patch keeps setjmp out of ldo.c, and the
  # shim's strong definitions keep wasi-importing libc members out of the
  # link. The stdout vtable's unreachable fd_* imports strip via wasm-opt.
  find_program(MOBAGEN_LUA_WASM_OPT NAMES wasm-opt HINTS "${CMAKE_SOURCE_DIR}/external/emsdk/upstream/bin" NO_CACHE)
  if(NOT MOBAGEN_LUA_WASM_OPT)
    find_program(MOBAGEN_LUA_WASM_OPT NAMES wasm-opt NO_CACHE)
  endif()
  if(MOBAGEN_LUA_WASM_OPT)
    add_custom_command(
      TARGET MobagenPortableLuaPlugin
      POST_BUILD
      COMMAND ${MOBAGEN_LUA_WASM_OPT} -all --remove-unused-module-elements --remove-imports
              "$<TARGET_FILE:MobagenPortableLuaPlugin>" -o "$<TARGET_FILE:MobagenPortableLuaPlugin>"
      VERBATIM
      COMMENT "Lua guest: stripping unreachable WASI imports (import-free standalone)"
    )
  else()
    message(FATAL_ERROR "scripting_lua emcc guest: wasm-opt not found (needed to strip WASI imports for an import-free guest)")
  endif()
  mobagen_lua_size_check_step(MobagenPortableLuaPlugin "$<TARGET_FILE:MobagenPortableLuaPlugin>")

  # Manifest extraction (todo 3 wiring): emcc emits no target_features
  # section, so the capability is asserted via --shared-memory (todo 18).
  if(NOT EMSCRIPTEN AND MOBAGEN_BUILD_MODULE_TOOLS AND MOBAGEN_BUILD_MANIFEST_TOOLS)
    add_dependencies(MobagenPortableLuaPlugin MobagenModuleManifest)
    set(tool "$<TARGET_FILE:MobagenModuleManifest>")
  else()
    unset(MOBAGEN_MODULE_MANIFEST_TOOL CACHE)
    find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest NO_DEFAULT_PATH NO_CMAKE_ENVIRONMENT_PATH)
    if(NOT MOBAGEN_MODULE_MANIFEST_TOOL)
      find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest PATHS "${CMAKE_SOURCE_DIR}/build-w1t1/bin" NO_DEFAULT_PATH)
    endif()
    set(tool "${MOBAGEN_MODULE_MANIFEST_TOOL}")
  endif()
  if(tool)
    add_custom_command(
      TARGET MobagenPortableLuaPlugin
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E echo "module manifest extraction (best-effort): $<TARGET_FILE:MobagenPortableLuaPlugin>"
      COMMAND
        ${tool} manifest --shared-memory "$<TARGET_FILE:MobagenPortableLuaPlugin>"
        "$<TARGET_FILE_DIR:MobagenPortableLuaPlugin>/module.manifest" || cmake -E true
      VERBATIM
      COMMENT "generating module.manifest for the portable Lua guest"
    )
  else()
    message(STATUS "scripting_lua guest: module manifest tool not found; skipping manifest POST_BUILD step")
  endif()
else()
  # Native-runtime guest (wasi-sdk, todo 18 pattern). -nostdlib keeps the
  # module import-free; math/ctype/time come from the sysroot libc.a
  # members assembled by lua_math_archive.cmake. Skips loudly when the
  # toolchain is absent.
  find_program(
    MOBAGEN_WASI_SDK_CLANG NAMES clang
    HINTS "${CMAKE_SOURCE_DIR}/external/wasi-sdk-34/bin"
    NO_DEFAULT_PATH
  )
  if(MOBAGEN_WASI_SDK_CLANG)
    set(MOBAGEN_WASI_SYSROOT "${CMAKE_SOURCE_DIR}/external/wasi-sdk-34/share/wasi-sysroot")
    set(MOBAGEN_LUA_MATH_ARCHIVE "${CMAKE_BINARY_DIR}/modules/scripting_lua/mobagen-lua-math.a")
    set(MOBAGEN_LUA_WASI_GUEST_DIR "${CMAKE_BINARY_DIR}/plugins-wasi/mobagen.scripting.lua.plugin")
    set(MOBAGEN_LUA_WASI_GUEST "${MOBAGEN_LUA_WASI_GUEST_DIR}/plugin.wasm")
    add_custom_command(
      OUTPUT ${MOBAGEN_LUA_MATH_ARCHIVE}
      COMMAND
        ${CMAKE_COMMAND}
        -DMOBAGEN_LUA_WASI_SYSROOT=${MOBAGEN_WASI_SYSROOT}
        -DMOBAGEN_LUA_MATH_ARCHIVE=${MOBAGEN_LUA_MATH_ARCHIVE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/lua_math_archive.cmake
      DEPENDS ${MOBAGEN_WASI_SYSROOT}/lib/wasm32-wasip1/libc.a
              ${CMAKE_CURRENT_SOURCE_DIR}/lua_math_archive.cmake
      COMMENT "assembling the import-free math archive for the Lua guest"
      VERBATIM
    )
    add_custom_command(
      OUTPUT ${MOBAGEN_LUA_WASI_GUEST}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${MOBAGEN_LUA_WASI_GUEST_DIR}
      COMMAND
        ${MOBAGEN_WASI_SDK_CLANG} --target=wasm32-wasip1 --sysroot=${MOBAGEN_WASI_SYSROOT} -Os -DNDEBUG -DMOBAGEN_LUA_TRAP_RECOVERY=1
        -Wno-deprecated-declarations -nostdlib
        -matomics -mbulk-memory ${MOBAGEN_LUA_FREESTANDING_INCLUDES} -Wl,--no-entry -Wl,--export-memory
        ${MOBAGEN_LUA_GUEST_WASM_EXPORTS} ${MOBAGEN_LUA_FREESTANDING_SOURCES} ${MOBAGEN_LUA_MATH_ARCHIVE}
        -o ${MOBAGEN_LUA_WASI_GUEST}
      COMMAND
        ${CMAKE_COMMAND} -DMOBAGEN_LUA_WASM=${MOBAGEN_LUA_WASI_GUEST}
        -DMOBAGEN_LUA_MAX_BYTES=${MOBAGEN_LUA_MAX_WASM_BYTES}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/lua_size_check.cmake
      DEPENDS ${MOBAGEN_LUA_FREESTANDING_SOURCES} ${MOBAGEN_LUA_MATH_ARCHIVE} ${MOBAGEN_LUA_PATCH_MARKER}
      COMMENT "compiling portable Lua guest (wasi-sdk, shared-memory-capable)"
      VERBATIM
    )
    add_custom_target(MobagenPortableLuaPluginWasi DEPENDS ${MOBAGEN_LUA_WASI_GUEST})
    unset(MOBAGEN_MODULE_MANIFEST_TOOL CACHE)
    if(MOBAGEN_BUILD_MODULE_TOOLS AND MOBAGEN_BUILD_MANIFEST_TOOLS)
      set(MOBAGEN_LUA_WASI_MANIFEST_TOOL "$<TARGET_FILE:MobagenModuleManifest>")
      add_dependencies(MobagenPortableLuaPluginWasi MobagenModuleManifest)
    else()
      find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest NO_DEFAULT_PATH NO_CMAKE_ENVIRONMENT_PATH)
      if(NOT MOBAGEN_MODULE_MANIFEST_TOOL)
        find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest PATHS "${CMAKE_SOURCE_DIR}/build-w1t1/bin" NO_DEFAULT_PATH)
      endif()
      if(MOBAGEN_MODULE_MANIFEST_TOOL)
        set(MOBAGEN_LUA_WASI_MANIFEST_TOOL "${MOBAGEN_MODULE_MANIFEST_TOOL}")
      endif()
    endif()
    if(MOBAGEN_LUA_WASI_MANIFEST_TOOL)
      # AOT stage — WITHHELD for the Lua guest, with data: wamrc 2.4.5
      # (aarch64, every size-level 0..3 / opt-level) miscompiles this
      # guest's trap-unwind path — any Lua error (LUAI_THROW -> trap)
      # executes a folded successor with stale registers and SIGSEGVs the
      # host (deterministic; the identical wasm runs clean under the
      # interpreter; synthetic trap guests AOT fine — todo 17 learnings).
      # T22/T26 own the wamrc follow-up; the loader's interpreter fallback
      # (instantiate_prefer_aot, todo 7) serves the package meanwhile.
      add_custom_command(
        TARGET MobagenPortableLuaPluginWasi
        POST_BUILD
        COMMAND
          ${MOBAGEN_LUA_WASI_MANIFEST_TOOL} manifest "${MOBAGEN_LUA_WASI_GUEST}"
          "${MOBAGEN_LUA_WASI_GUEST_DIR}/module.manifest"
        VERBATIM
        COMMENT "generating module.manifest for the wasi Lua guest (interpreter-only payload)"
      )
    else()
      message(STATUS "scripting_lua guest: module manifest tool not found; skipping wasi manifest POST_BUILD step")
    endif()
  else()
    message(
      STATUS
        "scripting_lua guest: wasi-sdk not found (external/wasi-sdk-34/bin/clang); "
        "run `python3 scripts/toolchains.py fetch wasi-sdk` to enable the native wasm guest. Skipping."
    )
  endif()
endif()
