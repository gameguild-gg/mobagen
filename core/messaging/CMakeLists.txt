# messaging module — header-only (Notifier, Event/EventBus). No threads.
add_executable(messaging_demo messaging_demo.cpp)
target_include_directories(messaging_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(messaging_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
