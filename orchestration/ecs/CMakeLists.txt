# ecs module — header-only (SparseSet, Storage<T>, World). The C ABI facade
# (ecs_c.h) and Group come next.
foreach(demo ecs_demo world_demo ecs_bench ecs_group_bench)
    add_executable(${demo} ${demo}.cpp)
    target_include_directories(${demo} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${demo} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
