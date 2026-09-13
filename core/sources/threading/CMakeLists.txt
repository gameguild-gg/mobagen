# threading module — zero-allocation construction-thread ownership checks.
add_library(mobagen_core_threading INTERFACE)
add_library(mobagen::core_threading ALIAS mobagen_core_threading)
target_include_directories(mobagen_core_threading INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_threading INTERFACE cxx_std_20)
