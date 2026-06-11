# reactive module — header-only (Signal / Computed / Effect). No threads.
add_library(mobagen_core_reactive INTERFACE)
add_library(mobagen::core_reactive ALIAS mobagen_core_reactive)
target_include_directories(mobagen_core_reactive INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_reactive INTERFACE cxx_std_20)
