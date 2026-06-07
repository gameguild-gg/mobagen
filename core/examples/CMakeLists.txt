# Cross-module examples: the "app" layer that glues the modules together.
find_package(Threads REQUIRED)

# systems-as-jobs: ecs + jobs
add_executable(systems_demo systems_demo.cpp ${CMAKE_SOURCE_DIR}/core/jobs/scheduler.cpp)
target_include_directories(systems_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/core/ecs
    ${CMAKE_SOURCE_DIR}/core/jobs)
set_target_properties(systems_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(systems_demo PRIVATE Threads::Threads)

# engine tick: ALL modules composed (ecs + jobs + reactive + messaging + scene)
add_executable(engine_demo engine_demo.cpp ${CMAKE_SOURCE_DIR}/core/jobs/scheduler.cpp)
target_include_directories(engine_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/core/ecs
    ${CMAKE_SOURCE_DIR}/core/jobs
    ${CMAKE_SOURCE_DIR}/core/reactive
    ${CMAKE_SOURCE_DIR}/core/messaging
    ${CMAKE_SOURCE_DIR}/core/scene)
set_target_properties(engine_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(engine_demo PRIVATE Threads::Threads glm)

# DOD engine slice: the editor's core loop in miniature — composes resource +
# scene (ecs/transform) + input + render-bridge + scene-serialize. Verified via
# em++ + node while native is unavailable (see the source header).
add_executable(dod_engine_slice dod_engine_slice.cpp)
target_include_directories(dod_engine_slice PRIVATE
    ${CMAKE_SOURCE_DIR}/core/ecs
    ${CMAKE_SOURCE_DIR}/core/scene
    ${CMAKE_SOURCE_DIR}/core/render
    ${CMAKE_SOURCE_DIR}/core/input
    ${CMAKE_SOURCE_DIR}/core/resource)
set_target_properties(dod_engine_slice PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(dod_engine_slice PRIVATE glm)
