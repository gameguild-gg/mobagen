# net module — the distributed seam (transport + serialization). Needs threads.
find_package(Threads REQUIRED)
add_executable(net_demo net_demo.cpp)
target_include_directories(net_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(net_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(net_demo PRIVATE Threads::Threads)
