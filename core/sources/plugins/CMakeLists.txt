add_library(
  mobagen_core_plugins STATIC
  asset_store_v1.h
  fixed_issue_list.hpp
  plugin_package.cpp
  plugin_package.hpp
  plugin_host.cpp
  plugin_host.hpp
  plugin_abi.h
  runtime_tick_v1.h
  wasm_memory.cpp
  wasm_memory.hpp
  wasm_plugin_loader.cpp
  wasm_plugin_loader.hpp
  wasm_plugin_store.cpp
  wasm_plugin_store.hpp
  wasm_plugin_catalog.cpp
  wasm_plugin_catalog.hpp
  wasm_plugin_activation_set.cpp
  wasm_plugin_activation_set.hpp
  wasm_command_channel.cpp
  wasm_command_channel.hpp
  wasm_asset_store.cpp
  wasm_asset_store.hpp
  wasm_host_imports.cpp
  wasm_host_imports.hpp
  wasm_plugin_contract.cpp
  wasm_plugin_contract.hpp
  wasm_runtime.cpp
  wasm_runtime.hpp
)
add_library(mobagen::core_plugins ALIAS mobagen_core_plugins)
target_compile_features(mobagen_core_plugins PUBLIC c_std_11 cxx_std_20)
target_include_directories(mobagen_core_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mobagen_core_plugins PUBLIC mobagen::core_modules mobagen::plugin_sdk)
# v2 store install verifies module.manifest payload hashes (todo 21); both
# libs are behind MOBAGEN_BUILD_MANIFEST_TOOLS like the loader's use.
if(TARGET mobagen::module_manifest)
  target_link_libraries(mobagen_core_plugins PRIVATE mobagen::module_manifest)
endif()

if(MOBAGEN_WASM_BACKEND_WAMR)
  add_subdirectory(wamr)
  target_sources(mobagen_core_plugins PRIVATE wamr_backend.cpp wamr_backend.hpp)
  # PUBLIC: consumers of the static lib must link the WAMR runtime and see its
  # MOBAGEN_WAMR_AOT_BUILD compile definition (tests gate AOT cases on it).
  target_link_libraries(mobagen_core_plugins PUBLIC mobagen::wamr_vm)
endif()

# Browser backend (todo 8): delegates plugin compilation to the browser's own
# wasm engine via the synchronous WebAssembly.Module/Instance constructors.
# Emscripten-only; also builds the MobagenBrowserBackendSmoke node bundle.
if(EMSCRIPTEN)
  add_subdirectory(browser)
endif()
