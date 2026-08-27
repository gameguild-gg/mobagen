# render module — DOD scene data -> flat renderer commands.
#
# Header-only for now. It deliberately depends on scene/ecs metadata, not on any GPU API. The
# renderer consumes the output command list.
add_library(mobagen_core_render INTERFACE)
add_library(mobagen::core_render ALIAS mobagen_core_render)
target_include_directories(mobagen_core_render INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_render INTERFACE cxx_std_23)
target_link_libraries(mobagen_core_render INTERFACE mobagen::core_ecs mobagen::core_scene glm)
