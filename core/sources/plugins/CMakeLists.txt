add_library(mobagen_core_plugins INTERFACE)
add_library(mobagen::core_plugins ALIAS mobagen_core_plugins)
target_compile_features(mobagen_core_plugins INTERFACE c_std_11 cxx_std_20)
target_include_directories(mobagen_core_plugins INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
