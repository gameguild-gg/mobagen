add_library(
  mobagen_core_plugins STATIC
  plugin_activation.cpp
  plugin_activation.hpp
  plugin_activation_set.cpp
  plugin_activation_set.hpp
  plugin_catalog.cpp
  plugin_catalog.hpp
  plugin_contract.cpp
  plugin_contract.hpp
  plugin_loader.cpp
  plugin_loader.hpp
  plugin_store.cpp
  plugin_store.hpp
  plugin_host.cpp
  plugin_host.hpp
  plugin_abi.h
  runtime_tick_v1.h
  wasm_memory.cpp
  wasm_memory.hpp
  wasm_plugin_contract.cpp
  wasm_plugin_contract.hpp
  wasm_runtime.cpp
  wasm_runtime.hpp
)
add_library(mobagen::core_plugins ALIAS mobagen_core_plugins)
target_compile_features(mobagen_core_plugins PUBLIC c_std_11 cxx_std_20)
target_include_directories(mobagen_core_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mobagen_core_plugins PUBLIC mobagen::core_modules mobagen::plugin_sdk)
target_link_libraries(mobagen_core_plugins PRIVATE ${CMAKE_DL_LIBS})
