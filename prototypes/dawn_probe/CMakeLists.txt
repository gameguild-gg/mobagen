# Standalone Dawn risk-gate (isolated from the renderer). Configure fetches Dawn
# + its deps (gigabytes, ~minutes); build compiles webgpu_dawn (~5-10 min cold).
#   cmake -B build/dawn-probe -S prototypes/dawn_probe -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/dawn-probe --config Release
cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(dawn_probe CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../external/cpm.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../external/dawn.cmake)

add_executable(dawn_probe main.cpp)
set_target_properties(dawn_probe PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(dawn_probe PRIVATE mobagen::webgpu)
