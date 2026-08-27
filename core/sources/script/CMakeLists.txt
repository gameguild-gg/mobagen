# script module — scripting support (requires BUILD_SCRIPT=ON).
#
# All implementation is currently commented out pending quickjspp integration. Exposed as INTERFACE
# so the include path is available to consumers.
add_library(mobagen_core_script INTERFACE)
add_library(mobagen::core_script ALIAS mobagen_core_script)
target_include_directories(mobagen_core_script INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
