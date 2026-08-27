# input module — SDL-agnostic input state for DOD systems (header-only).
#
# The platform adapter (SDL3 in the renderer) feeds events in; systems query edges/held/deltas.
# Tests live in apps/core_demos.
add_library(mobagen_core_input INTERFACE)
add_library(mobagen::core_input ALIAS mobagen_core_input)
target_include_directories(mobagen_core_input INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_input INTERFACE cxx_std_20)
