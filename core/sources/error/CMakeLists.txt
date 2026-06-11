# error module — header-only error/exception utilities (NotImplemented.h).
add_library(mobagen_core_error INTERFACE)
add_library(mobagen::core_error ALIAS mobagen_core_error)
target_include_directories(mobagen_core_error INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
