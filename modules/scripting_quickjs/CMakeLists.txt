# QuickJS scripting guest (dynamic-loading-all-platforms todo 16).
#
# quickjs-ng v0.10.0 (MIT) fetched via CPM (external/quickjs-ng.cmake) —
# the engine lives ONLY inside the wasm guest, never embedded natively
# in the core. Mirrors asset_store/CMakeLists.txt: emcc standalone guest
# for the web trees, wasi-sdk guest for WAMR, manifest via the extraction
# tool, AOT stage (todo 20 helper) on desktop/android, and a HARD 8 MiB
# size assert (Chromium main-thread sync-compile budget — the browser
# sync path throws BackendFailure at that limit; the build fails earlier
# and louder).

include(${CMAKE_SOURCE_DIR}/external/quickjs-ng.cmake)

set(MOBAGEN_QUICKJS_GUEST_WASM_EXPORTS
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
    -Wl,--export=mobagen_module_exports_v1
    -Wl,--export=mobagen_module_entry_v1)

set(MOBAGEN_QUICKJS_FREESTANDING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_libc_shim.c
    ${quickjs_SOURCE_DIR}/quickjs.c
    ${quickjs_SOURCE_DIR}/cutils.c
    ${quickjs_SOURCE_DIR}/libregexp.c
    ${quickjs_SOURCE_DIR}/libunicode.c
    ${quickjs_SOURCE_DIR}/xsum.c
    ${CMAKE_CURRENT_SOURCE_DIR}/portable_quickjs_plugin.c)

set(MOBAGEN_QUICKJS_FREESTANDING_INCLUDES
    -isystem ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers
    -I${quickjs_SOURCE_DIR}
    -I${CMAKE_SOURCE_DIR}/sdk/include)

# Chromium main-thread sync-compile budget (review rr-dlap-1). The 8MB limit
# is a THROW in the browser sync path; this assert makes an oversized guest a
# BUILD failure instead. Checked at generate time in script mode below.
set(MOBAGEN_QUICKJS_MAX_WASM_BYTES 8388608)

# Shared size gate + manifest helper. `cmake -E env` runs the checker so the
# same command serves custom_command (wasi) and POST_BUILD (emcc).
function(mobagen_quickjs_size_check_step target wasm_path)
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -DMOBAGEN_QUICKJS_WASM=${wasm_path} -DMOBAGEN_QUICKJS_MAX_BYTES=${MOBAGEN_QUICKJS_MAX_WASM_BYTES}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/quickjs_size_check.cmake
    VERBATIM
    COMMENT "QuickJS guest size assert (< ${MOBAGEN_QUICKJS_MAX_WASM_BYTES} bytes)"
  )
endfunction()

if(EMSCRIPTEN)
  add_executable(MobagenPortableQuickJSPlugin ${MOBAGEN_QUICKJS_FREESTANDING_SOURCES})
  target_compile_features(MobagenPortableQuickJSPlugin PRIVATE c_std_11)
  target_link_libraries(MobagenPortableQuickJSPlugin PRIVATE mobagen::plugin_sdk)
  # emcc links musl: NO shadow headers on this path — the freestanding shim
  # compiles down to its clock helper only (see freestanding_libc_shim.c).
  target_include_directories(MobagenPortableQuickJSPlugin PRIVATE
    ${quickjs_SOURCE_DIR})
  # Capability, not thread usage (todo 18): =0 overrides come LAST because the
  # root CMakeLists appends the shared variant's USE_PTHREADS/SHARED_MEMORY
  # to every target (emcc last-flag-wins).
  target_link_options(
    MobagenPortableQuickJSPlugin
    PRIVATE
    -sSTANDALONE_WASM=1
    -sALLOW_MEMORY_GROWTH=0
    -sUSE_PTHREADS=0
    -sSHARED_MEMORY=0
    -matomics
    -mbulk-memory
    -Wl,--no-entry
    -Wl,--export-memory
    ${MOBAGEN_QUICKJS_GUEST_WASM_EXPORTS}
  )
  set_target_properties(
    MobagenPortableQuickJSPlugin
    PROPERTIES OUTPUT_NAME plugin SUFFIX ".wasm"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/mobagen.scripting.quickjs.plugin"
  )
  # musl's stdio vtable keeps an (unreachable-from-our-exports) wasi
  # fd_write import alive through the exported indirect function table;
  # both WAMR and the browser backend instantiate WITHOUT WASI imports, so
  # the link step strips unused module elements + the import. wasm-opt runs
  # with -all (the guest uses bulk-memory/atomics/sign-ext opcodes).
  find_program(MOBAGEN_QUICKJS_WASM_OPT NAMES wasm-opt HINTS "${CMAKE_SOURCE_DIR}/external/emsdk/upstream/bin" NO_CACHE)
  if(NOT MOBAGEN_QUICKJS_WASM_OPT)
    find_program(MOBAGEN_QUICKJS_WASM_OPT NAMES wasm-opt NO_CACHE)
  endif()
  if(MOBAGEN_QUICKJS_WASM_OPT)
    add_custom_command(
      TARGET MobagenPortableQuickJSPlugin
      POST_BUILD
      COMMAND ${MOBAGEN_QUICKJS_WASM_OPT} -all --remove-unused-module-elements --remove-imports
              "$<TARGET_FILE:MobagenPortableQuickJSPlugin>" -o "$<TARGET_FILE:MobagenPortableQuickJSPlugin>"
      VERBATIM
      COMMENT "QuickJS guest: stripping unreachable WASI imports (import-free standalone)"
    )
  else()
    message(FATAL_ERROR "scripting_quickjs emcc guest: wasm-opt not found (needed to strip WASI imports for an import-free guest)")
  endif()
  mobagen_quickjs_size_check_step(MobagenPortableQuickJSPlugin "$<TARGET_FILE:MobagenPortableQuickJSPlugin>")

  # Manifest extraction (todo 3 wiring). emcc emits no target_features
  # section, so the capability is asserted via --shared-memory (todo 18).
  if(NOT EMSCRIPTEN AND MOBAGEN_BUILD_MODULE_TOOLS AND MOBAGEN_BUILD_MANIFEST_TOOLS)
    add_dependencies(MobagenPortableQuickJSPlugin MobagenModuleManifest)
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
      TARGET MobagenPortableQuickJSPlugin
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E echo "module manifest extraction (best-effort): $<TARGET_FILE:MobagenPortableQuickJSPlugin>"
      COMMAND
        ${tool} manifest --shared-memory "$<TARGET_FILE:MobagenPortableQuickJSPlugin>"
        "$<TARGET_FILE_DIR:MobagenPortableQuickJSPlugin>/module.manifest" || cmake -E true
      VERBATIM
      COMMENT "generating module.manifest for the portable QuickJS guest"
    )
  else()
    message(STATUS "scripting_quickjs guest: module manifest tool not found; skipping manifest POST_BUILD step")
  endif()
else()
  # Native-runtime guest (wasi-sdk, todo 18 pattern): same freestanding
  # source set; -nostdlib keeps the module import-free (WAMR resolves no
  # WASI imports). Skips loudly when the toolchain is absent.
  find_program(
    MOBAGEN_WASI_SDK_CLANG NAMES clang
    HINTS "${CMAKE_SOURCE_DIR}/external/wasi-sdk-34/bin"
    NO_DEFAULT_PATH
  )
  if(MOBAGEN_WASI_SDK_CLANG)
    set(MOBAGEN_WASI_SYSROOT "${CMAKE_SOURCE_DIR}/external/wasi-sdk-34/share/wasi-sysroot")
    set(MOBAGEN_QUICKJS_WASI_GUEST_DIR "${CMAKE_BINARY_DIR}/plugins-wasi/mobagen.scripting.quickjs.plugin")
    set(MOBAGEN_QUICKJS_WASI_GUEST "${MOBAGEN_QUICKJS_WASI_GUEST_DIR}/plugin.wasm")
    add_custom_command(
      OUTPUT ${MOBAGEN_QUICKJS_WASI_GUEST}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${MOBAGEN_QUICKJS_WASI_GUEST_DIR}
      COMMAND
        ${MOBAGEN_WASI_SDK_CLANG} --target=wasm32-wasip1 --sysroot=${MOBAGEN_WASI_SYSROOT} -Os -DNDEBUG -D_GNU_SOURCE -nostdlib
        -matomics -mbulk-memory ${MOBAGEN_QUICKJS_FREESTANDING_INCLUDES} -Wl,--no-entry -Wl,--export-memory
        ${MOBAGEN_QUICKJS_GUEST_WASM_EXPORTS} ${MOBAGEN_QUICKJS_FREESTANDING_SOURCES}
        -o ${MOBAGEN_QUICKJS_WASI_GUEST}
      COMMAND
        ${CMAKE_COMMAND} -DMOBAGEN_QUICKJS_WASM=${MOBAGEN_QUICKJS_WASI_GUEST}
        -DMOBAGEN_QUICKJS_MAX_BYTES=${MOBAGEN_QUICKJS_MAX_WASM_BYTES}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/quickjs_size_check.cmake
      DEPENDS ${MOBAGEN_QUICKJS_FREESTANDING_SOURCES}
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/stdio.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/stdlib.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/math.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/time.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/string.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/assert.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/inttypes.h
              ${CMAKE_CURRENT_SOURCE_DIR}/freestanding_headers/sys/time.h
              ${CMAKE_SOURCE_DIR}/sdk/include/mobagen/module/module_abi.h
      COMMENT "compiling portable QuickJS guest (wasi-sdk, shared-memory-capable)"
      VERBATIM
    )
    add_custom_target(MobagenPortableQuickJSPluginWasi DEPENDS ${MOBAGEN_QUICKJS_WASI_GUEST})
    unset(MOBAGEN_MODULE_MANIFEST_TOOL CACHE)
    if(MOBAGEN_BUILD_MODULE_TOOLS AND MOBAGEN_BUILD_MANIFEST_TOOLS)
      set(MOBAGEN_QUICKJS_WASI_MANIFEST_TOOL "$<TARGET_FILE:MobagenModuleManifest>")
      add_dependencies(MobagenPortableQuickJSPluginWasi MobagenModuleManifest)
    else()
      find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest NO_DEFAULT_PATH NO_CMAKE_ENVIRONMENT_PATH)
      if(NOT MOBAGEN_MODULE_MANIFEST_TOOL)
        find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest PATHS "${CMAKE_SOURCE_DIR}/build-w1t1/bin" NO_DEFAULT_PATH)
      endif()
      if(MOBAGEN_MODULE_MANIFEST_TOOL)
        set(MOBAGEN_QUICKJS_WASI_MANIFEST_TOOL "${MOBAGEN_MODULE_MANIFEST_TOOL}")
      endif()
    endif()
    if(MOBAGEN_QUICKJS_WASI_MANIFEST_TOOL)
      # AOT stage (todo 20/21): REQUIRED when the pinned wamrc cache resolves.
      include(${CMAKE_SOURCE_DIR}/cmake/mobagen_module_aot.cmake)
      set(_MOBAGEN_QUICKJS_AOT_ARGS "")
      if(EXISTS "${CMAKE_SOURCE_DIR}/external/wamrc-cache/wamrc/wamrc" OR MOBAGEN_WAMRC_EXECUTABLE)
        set(_MOBAGEN_QUICKJS_AOT_ARGS REQUIRED)
      endif()
      mobagen_add_module_aot_stage(
        MobagenPortableQuickJSPluginWasi
        WASM ${MOBAGEN_QUICKJS_WASI_GUEST}
        MANIFEST_TOOL ${MOBAGEN_QUICKJS_WASI_MANIFEST_TOOL}
        ${_MOBAGEN_QUICKJS_AOT_ARGS}
      )
      # Cross aarch64 output must not live inside the package dir (loader
      # whitelist); relocate one level up for android packaging (todo 21).
      if(TARGET MobagenPortableQuickJSPluginWasi_aot_stage)
        set(MOBAGEN_QUICKJS_AARCH64_CROSS_DIR "${CMAKE_BINARY_DIR}/plugins-wasi-aarch64-cross-quickjs")
        add_custom_command(
          TARGET MobagenPortableQuickJSPluginWasi_aot_stage
          POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory ${MOBAGEN_QUICKJS_AARCH64_CROSS_DIR}
          COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${MOBAGEN_QUICKJS_WASI_GUEST_DIR}/plugin.aarch64.aot"
            "${MOBAGEN_QUICKJS_AARCH64_CROSS_DIR}/plugin.aarch64.aot"
          COMMAND
            ${CMAKE_COMMAND} -E remove "${MOBAGEN_QUICKJS_WASI_GUEST_DIR}/plugin.aarch64.aot"
          VERBATIM
          COMMENT "relocating aarch64 cross AOT out of the QuickJS package dir"
        )
      endif()
      if(NOT TARGET MobagenPortableQuickJSPluginWasi_aot_stage)
        add_custom_command(
          TARGET MobagenPortableQuickJSPluginWasi
          POST_BUILD
          COMMAND
            ${CMAKE_COMMAND} -E echo "module manifest extraction (best-effort): ${MOBAGEN_QUICKJS_WASI_GUEST}"
          COMMAND
            ${MOBAGEN_QUICKJS_WASI_MANIFEST_TOOL} manifest "${MOBAGEN_QUICKJS_WASI_GUEST}"
            "${MOBAGEN_QUICKJS_WASI_GUEST_DIR}/module.manifest" || cmake -E true
          VERBATIM
          COMMENT "generating module.manifest for the wasi QuickJS guest"
        )
      endif()
    else()
      message(STATUS "scripting_quickjs guest: module manifest tool not found; skipping wasi manifest POST_BUILD step")
    endif()
  else()
    message(
      STATUS
        "scripting_quickjs guest: wasi-sdk not found (external/wasi-sdk-34/bin/clang); "
        "run `python3 scripts/toolchains.py fetch wasi-sdk` to enable the native wasm guest. Skipping."
    )
  endif()
endif()
