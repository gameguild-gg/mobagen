if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Standalone Dawn risk-gate. Configure fetches Dawn + its deps (gigabytes, ~minutes); build
  # compiles webgpu_dawn (~5-10 min cold).
  cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  project(dawn_probe CXX)
  include(${CMAKE_CURRENT_LIST_DIR}/../../external/cpm.cmake)
endif()

if(NOT TARGET dawn::webgpu)
  include(${CMAKE_CURRENT_LIST_DIR}/../../external/dawn.cmake)
endif()

add_executable(dawn_probe sources/main.cpp)
set_target_properties(dawn_probe PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(dawn_probe PRIVATE dawn::webgpu)
