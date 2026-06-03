# scene module — Transform + TransformSystem on the ECS. Header-only; needs glm.
foreach(t scene_demo scene_bench)
    add_executable(${t} ${t}.cpp)
    target_include_directories(${t} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/ecs)
    set_target_properties(${t} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${t} PRIVATE glm)
endforeach()

# SIMD for the transform benchmark (glm intrinsics + AVX2 for the matrix math)
target_compile_definitions(scene_bench PRIVATE GLM_FORCE_INTRINSICS)
if(MSVC)
    target_compile_options(scene_bench PRIVATE /arch:AVX2)
else()
    target_compile_options(scene_bench PRIVATE -mavx2)
endif()
