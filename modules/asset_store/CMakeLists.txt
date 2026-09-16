set_target_properties(
  mobagen_core_assets mobagen_asset_store_service PROPERTIES POSITION_INDEPENDENT_CODE ON
)

if(EMSCRIPTEN)
  add_executable(MobagenPortableAssetStorePlugin portable_asset_store_plugin.c)
  target_compile_features(MobagenPortableAssetStorePlugin PRIVATE c_std_11)
  target_link_libraries(MobagenPortableAssetStorePlugin PRIVATE mobagen::plugin_sdk)
  target_link_options(
    MobagenPortableAssetStorePlugin
    PRIVATE
    -sSTANDALONE_WASM=1
    -sALLOW_MEMORY_GROWTH=0
    -Wl,--no-entry
    -Wl,--export-memory
    -Wl,--export=mobagen_wasm_plugin_allocate_v1
    -Wl,--export=mobagen_wasm_plugin_deallocate_v1
    -Wl,--export=mobagen_wasm_plugin_query_v1
    -Wl,--export=mobagen_wasm_plugin_configure_v1
    -Wl,--export=mobagen_wasm_plugin_start_v1
    -Wl,--export=mobagen_wasm_plugin_quiesce_v1
    -Wl,--export=mobagen_wasm_plugin_stop_v1
    -Wl,--export=mobagen_wasm_plugin_process_v1
  )
  set_target_properties(
    MobagenPortableAssetStorePlugin
    PROPERTIES OUTPUT_NAME plugin SUFFIX ".wasm"
               RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/mobagen.assets.default.plugin"
  )
else()
  add_library(MobagenDefaultAssetStorePlugin MODULE default_asset_store_plugin.cpp)
  target_compile_features(MobagenDefaultAssetStorePlugin PRIVATE cxx_std_20)
  target_compile_definitions(MobagenDefaultAssetStorePlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
  target_link_libraries(
    MobagenDefaultAssetStorePlugin PRIVATE mobagen::asset_store_service mobagen::plugin_sdk
  )
  set_target_properties(MobagenDefaultAssetStorePlugin PROPERTIES PREFIX "")
endif()
