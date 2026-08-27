# datastructures module — grid, tree, and vector data structures.
#
# Header-only INTERFACE target: all substantive types are templates and are instantiated by the
# consumer. Grid2D.h currently depends on math/Point2D.h which was removed in W0-A; it is preserved
# for reference only and must not be compiled until that dependency is resolved.
add_library(mobagen_core_datastructures INTERFACE)
add_library(mobagen::core_datastructures ALIAS mobagen_core_datastructures)
target_include_directories(mobagen_core_datastructures INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_datastructures INTERFACE cxx_std_20)
