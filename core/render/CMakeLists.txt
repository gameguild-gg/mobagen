# render module — DOD scene data -> flat renderer commands.
#
# Header-only for now. The demo proves the bridge can collect a DICOM-like
# volume entity from the ECS without linking any GPU API.
add_executable(render_bridge_demo render_bridge_demo.cpp)
target_include_directories(render_bridge_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/ecs
    ${CMAKE_SOURCE_DIR}/core/scene)
set_target_properties(render_bridge_demo PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(render_bridge_demo PRIVATE glm)
