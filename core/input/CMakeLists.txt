# input module — SDL-agnostic input state for DOD systems (header-only).
# The platform adapter (SDL3 in the renderer) feeds events in; systems query
# edges/held/deltas. Test verified via em++ + node while native is unavailable
# (see the test header for the command).
add_executable(input_state_test input_state_test.cpp)
target_include_directories(input_state_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(input_state_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
