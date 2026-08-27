# net module — the distributed seam (transport + serialization). Header-only, but its loopback
# transport uses std::thread, so consumers need Threads.
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
endif()

add_library(mobagen_core_net INTERFACE)
add_library(mobagen::core_net ALIAS mobagen_core_net)
target_include_directories(mobagen_core_net INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mobagen_core_net INTERFACE cxx_std_20)
if(NOT EMSCRIPTEN)
  target_link_libraries(mobagen_core_net INTERFACE Threads::Threads)
endif()
