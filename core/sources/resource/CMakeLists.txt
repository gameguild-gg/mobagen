# resource module — stable-handle asset store (header-only). Holds the big CPU (later GPU) assets
# the renderer references by handle.
add_library(mobagen_core_resource INTERFACE)
add_library(mobagen::core_resource ALIAS mobagen_core_resource)
target_include_directories(mobagen_core_resource INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_resource INTERFACE cxx_std_20)
