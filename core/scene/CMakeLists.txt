# scene module — Transform + TransformSystem on the ECS. Header-only; needs glm.
add_executable(scene_demo scene_demo.cpp)
target_include_directories(scene_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/ecs)
set_target_properties(scene_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(scene_demo PRIVATE glm)
