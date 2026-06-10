# messaging module — header-only (Notifier, Event/EventBus). No threads.
add_library(mobagen_core_messaging INTERFACE)
add_library(mobagen::core_messaging ALIAS mobagen_core_messaging)
target_include_directories(mobagen_core_messaging INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_messaging INTERFACE cxx_std_20)
