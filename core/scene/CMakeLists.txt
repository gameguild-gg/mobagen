# scene module — Transform + TransformSystem on the ECS. Header-only; needs glm.
add_library(mobagen_core_scene INTERFACE)
add_library(mobagen::core_scene ALIAS mobagen_core_scene)
target_include_directories(mobagen_core_scene INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_scene INTERFACE cxx_std_20)
target_link_libraries(mobagen_core_scene INTERFACE mobagen::core_ecs glm)
