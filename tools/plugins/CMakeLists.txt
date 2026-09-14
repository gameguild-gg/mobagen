add_library(mobagen_plugin_cli STATIC plugin_cli.cpp plugin_cli.hpp)
add_library(mobagen::plugin_cli ALIAS mobagen_plugin_cli)
target_compile_features(mobagen_plugin_cli PUBLIC cxx_std_20)
target_include_directories(mobagen_plugin_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mobagen_plugin_cli PUBLIC mobagen::core_plugins)

add_executable(MobagenPlugins main.cpp)
target_compile_features(MobagenPlugins PRIVATE cxx_std_20)
target_link_libraries(MobagenPlugins PRIVATE mobagen::plugin_cli)
