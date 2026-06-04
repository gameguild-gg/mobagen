# reactive module — header-only (Signal / Computed / Effect). No threads.
add_executable(reactive_demo reactive_demo.cpp)
target_include_directories(reactive_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(reactive_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
