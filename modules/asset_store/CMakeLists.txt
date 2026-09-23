set_target_properties(
  mobagen_core_assets mobagen_asset_store_service PROPERTIES POSITION_INDEPENDENT_CODE ON
)

# Portable wasm guest (todo 18): every participating guest is compiled
# shared-memory-CAPABLE (atomics + bulk-memory). This is a capability, not a
# thread-spawn requirement — the threads policy stays `none`. The emscripten
# guest additionally links against the shared variant of the web runtime when
# MOBAGEN_WEB_SHARED (the flags make its linear memory shareable); the
# native-runtime guest is built by wasi-sdk clang with -matomics
# -mbulk-memory, which stamps a `+atomics` target_features section the
# manifest tool detects (shared-memory: true).
set(MOBAGEN_ASSET_STORE_GUEST_WASM_EXPORTS
    -Wl,--export=mobagen_wasm_plugin_allocate_v1
    -Wl,--export=mobagen_wasm_plugin_deallocate_v1
    -Wl,--export=mobagen_wasm_plugin_query_v1
    -Wl,--export=mobagen_wasm_plugin_configure_v1
    -Wl,--export=mobagen_wasm_plugin_start_v1
    -Wl,--export=mobagen_wasm_plugin_quiesce_v1
    -Wl,--export=mobagen_wasm_plugin_stop_v1
    -Wl,--export=mobagen_wasm_plugin_process_v1
    -Wl,--export=mobagen_module_exports_v1
    -Wl,--export=mobagen_module_entry_v1)

function(mobagen_asset_store_guest_manifest_step target)
  # Signature-extraction POST_BUILD step (todo 3 wiring; todo 18's annotation
  # table makes it succeed). This tree's own tool via generator expression
  # when module tools are enabled; otherwise the cached cross-build path.
  # unset() first: find_program caches its first hit forever, and a stale
  # entry pointing into another build tree would run outdated extraction code.
  if(NOT EMSCRIPTEN AND MOBAGEN_BUILD_MODULE_TOOLS AND MOBAGEN_BUILD_MANIFEST_TOOLS)
    add_dependencies(${target} MobagenModuleManifest)
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
    # emcc emits no target_features section, so its compile flags (-matomics
    # -mbulk-memory above) are asserted to the tool instead of detected.
    if(EMSCRIPTEN)
      set(extra_flags "--shared-memory")
    else()
      set(extra_flags "")
    endif()
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E echo "module manifest extraction (best-effort): $<TARGET_FILE:${target}>"
      COMMAND
        ${tool} manifest ${extra_flags} "$<TARGET_FILE:${target}>"
        "$<TARGET_FILE_DIR:${target}>/module.manifest" || cmake -E true
      VERBATIM
      COMMENT "generating module.manifest for the portable asset store guest"
    )
  else()
    message(STATUS "asset_store guest: module manifest tool not found; skipping manifest POST_BUILD step")
  endif()
endfunction()

if(EMSCRIPTEN)
  add_executable(MobagenPortableAssetStorePlugin portable_asset_store_plugin.c)
  target_compile_features(MobagenPortableAssetStorePlugin PRIVATE c_std_11)
  target_link_libraries(MobagenPortableAssetStorePlugin PRIVATE mobagen::plugin_sdk)
  # Capability, not thread usage: -matomics -mbulk-memory compile the guest
  # shared-memory-CAPABLE while USE_PTHREADS stays 0 — the guest spawns no
  # threads, so it never links against emscripten's pthread ABI (imports that
  # a non-emscripten runtime cannot provide). The =0 flags come LAST because
  # the root CMakeLists appends the shared variant's USE_PTHREADS/SHARED_MEMORY
  # to every target (last flag wins in emcc).
  target_link_options(
    MobagenPortableAssetStorePlugin
    PRIVATE
    -sSTANDALONE_WASM=1
    -sALLOW_MEMORY_GROWTH=0
    -sUSE_PTHREADS=0
    -sSHARED_MEMORY=0
    -matomics
    -mbulk-memory
    -Wl,--no-entry
    -Wl,--export-memory
    ${MOBAGEN_ASSET_STORE_GUEST_WASM_EXPORTS}
  )
  set_target_properties(
    MobagenPortableAssetStorePlugin
    PROPERTIES OUTPUT_NAME plugin SUFFIX ".wasm"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/mobagen.assets.default.plugin"
  )
  mobagen_asset_store_guest_manifest_step(MobagenPortableAssetStorePlugin)
else()
  # Native-runtime guest (todo 18): built with the fetched wasi-sdk
  # (scripts/toolchains.py fetch wasi-sdk -> external/wasi-sdk-34) so WAMR-side
  # tests exercise the same source as the web guest. Shared-memory-capable
  # flags; -nostdlib keeps the module import-free (WAMR resolves no WASI
  # imports). Skipped with a clear message when the toolchain is absent.
  find_program(
    MOBAGEN_WASI_SDK_CLANG NAMES clang
    HINTS "${CMAKE_SOURCE_DIR}/external/wasi-sdk-34/bin"
    NO_DEFAULT_PATH
  )
  if(MOBAGEN_WASI_SDK_CLANG)
    set(MOBAGEN_WASI_SYSROOT "${CMAKE_SOURCE_DIR}/external/wasi-sdk-34/share/wasi-sysroot")
    set(MOBAGEN_ASSET_STORE_WASI_GUEST_DIR "${CMAKE_BINARY_DIR}/plugins-wasi/mobagen.assets.default.plugin")
    set(MOBAGEN_ASSET_STORE_WASI_GUEST "${MOBAGEN_ASSET_STORE_WASI_GUEST_DIR}/plugin.wasm")
    add_custom_command(
      OUTPUT ${MOBAGEN_ASSET_STORE_WASI_GUEST}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${MOBAGEN_ASSET_STORE_WASI_GUEST_DIR}
      COMMAND
        ${MOBAGEN_WASI_SDK_CLANG} --target=wasm32-wasip1 --sysroot=${MOBAGEN_WASI_SYSROOT} -O2 -nostdlib
        -matomics -mbulk-memory -I${CMAKE_SOURCE_DIR}/sdk/include -Wl,--no-entry -Wl,--export-memory
        ${MOBAGEN_ASSET_STORE_GUEST_WASM_EXPORTS} ${CMAKE_CURRENT_SOURCE_DIR}/portable_asset_store_plugin.c
        -o ${MOBAGEN_ASSET_STORE_WASI_GUEST}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/portable_asset_store_plugin.c
              ${CMAKE_SOURCE_DIR}/sdk/include/mobagen/module/module_abi.h
      COMMENT "compiling portable asset store guest (wasi-sdk, shared-memory-capable)"
      VERBATIM
    )
    add_custom_target(MobagenPortableAssetStorePluginWasi DEPENDS ${MOBAGEN_ASSET_STORE_WASI_GUEST})
    # Manifest tool: this tree's own target when module tools are enabled
    # (tools/modules configures AFTER this directory, so the generator
    # expression resolves at generate time — order-independent), else the
    # cached cross-build path. unset() first: find_program caches the first
    # hit forever, and a stale entry pointing into build-w1t1 would run an
    # outdated extraction binary.
    unset(MOBAGEN_MODULE_MANIFEST_TOOL CACHE)
    if(MOBAGEN_BUILD_MODULE_TOOLS AND MOBAGEN_BUILD_MANIFEST_TOOLS)
      set(MOBAGEN_ASSET_STORE_WASI_MANIFEST_TOOL "$<TARGET_FILE:MobagenModuleManifest>")
      add_dependencies(MobagenPortableAssetStorePluginWasi MobagenModuleManifest)
    else()
      find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest NO_DEFAULT_PATH NO_CMAKE_ENVIRONMENT_PATH)
      if(NOT MOBAGEN_MODULE_MANIFEST_TOOL)
        find_program(MOBAGEN_MODULE_MANIFEST_TOOL NAMES MobagenModuleManifest PATHS "${CMAKE_SOURCE_DIR}/build-w1t1/bin" NO_DEFAULT_PATH)
      endif()
      if(MOBAGEN_MODULE_MANIFEST_TOOL)
        set(MOBAGEN_ASSET_STORE_WASI_MANIFEST_TOOL "${MOBAGEN_MODULE_MANIFEST_TOOL}")
      endif()
    endif()
    if(MOBAGEN_ASSET_STORE_WASI_MANIFEST_TOOL)
      add_custom_command(
        TARGET MobagenPortableAssetStorePluginWasi
        POST_BUILD
        COMMAND
          ${CMAKE_COMMAND} -E echo "module manifest extraction (best-effort): ${MOBAGEN_ASSET_STORE_WASI_GUEST}"
        COMMAND
          ${MOBAGEN_ASSET_STORE_WASI_MANIFEST_TOOL} manifest "${MOBAGEN_ASSET_STORE_WASI_GUEST}"
          "${MOBAGEN_ASSET_STORE_WASI_GUEST_DIR}/module.manifest" || cmake -E true
        VERBATIM
        COMMENT "generating module.manifest for the wasi asset store guest"
      )
    else()
      message(STATUS "asset_store guest: module manifest tool not found; skipping wasi manifest POST_BUILD step")
    endif()
  else()
    message(
      STATUS
        "asset_store guest: wasi-sdk not found (external/wasi-sdk-34/bin/clang); "
        "run `python3 scripts/toolchains.py fetch wasi-sdk` to enable the native wasm guest. Skipping."
    )
  endif()

  add_library(MobagenDefaultAssetStorePlugin MODULE default_asset_store_plugin.cpp)
  target_compile_features(MobagenDefaultAssetStorePlugin PRIVATE cxx_std_20)
  target_compile_definitions(MobagenDefaultAssetStorePlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
  target_link_libraries(
    MobagenDefaultAssetStorePlugin PRIVATE mobagen::asset_store_service mobagen::plugin_sdk
  )
  set_target_properties(MobagenDefaultAssetStorePlugin PROPERTIES PREFIX "")
endif()
