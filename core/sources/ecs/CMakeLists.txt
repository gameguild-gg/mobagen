# ecs module — header-only (SparseSet, Storage<T>, World, Group).
#
# This target is intentionally only a library/interface. Executable demos and benches live in
# apps/core_demos so core stays usable as a dependency.
add_library(mobagen_core_ecs INTERFACE)
add_library(mobagen::core_ecs ALIAS mobagen_core_ecs)
target_include_directories(mobagen_core_ecs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_ecs INTERFACE cxx_std_20)
