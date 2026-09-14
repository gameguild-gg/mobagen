add_library(
  mobagen_core_plugins STATIC plugin_contract.cpp plugin_contract.hpp plugin_loader.cpp
                              plugin_loader.hpp plugin_abi.h
)
add_library(mobagen::core_plugins ALIAS mobagen_core_plugins)
target_compile_features(mobagen_core_plugins PUBLIC c_std_11 cxx_std_20)
target_include_directories(mobagen_core_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mobagen_core_plugins PUBLIC mobagen::core_modules)
target_link_libraries(mobagen_core_plugins PRIVATE ${CMAKE_DL_LIBS})
