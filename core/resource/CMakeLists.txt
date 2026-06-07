# resource module — stable-handle asset store (header-only). Holds the big CPU
# (later GPU) assets the renderer references by handle. Test verified via em++ +
# node while the native toolchain is unavailable (see the test header).
add_executable(resource_registry_test resource_registry_test.cpp)
target_include_directories(resource_registry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(resource_registry_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
