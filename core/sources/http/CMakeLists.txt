add_library(mobagen_core_http INTERFACE)
add_library(mobagen::core_http ALIAS mobagen_core_http)
target_include_directories(mobagen_core_http INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_http INTERFACE cxx_std_20)
