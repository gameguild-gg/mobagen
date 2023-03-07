cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

# CPM downloader
include(external/cpm.cmake)

# project declaration
project(MoBaGEn
        VERSION 0.0.1
        HOMEPAGE_URL "https://infinbrains.com"
        DESCRIPTION "Module Based Game Engine"
)

# Include guards
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there.")
endif()

# compiler checks
include(external/compilerchecks.cmake)

# set output dirs
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# set linkage
LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/libs)
#LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/libs/Debug)

IF(EMSCRIPTEN)
    message(STATUS "emscripten build")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Oz -s WASM=2 -s USE_SDL=0 -s ASYNCIFY -s ASSERTIONS=1 -s ALLOW_MEMORY_GROWTH=1")
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Oz -s WASM=2 -s USE_SDL=0 -s ASYNCIFY -s ASSERTIONS=1 -s ALLOW_MEMORY_GROWTH=1")

    file(GLOB HTML "${CMAKE_CURRENT_SOURCE_DIR}/html/*")
    file(COPY ${HTML} DESTINATION ${CMAKE_BINARY_DIR}/bin/)

    SET(ENABLE_TEST_COVERAGE OFF)
ENDIF()

include(external/external.cmake)

add_subdirectory(core)

option(ENABLE_TEST_COVERAGE "ENABLE_TEST_COVERAGE" ON)
if(ENABLE_TEST_COVERAGE)
  add_subdirectory(test)
endif()

option(BUILD_EXAMPLES "BUILD_EXAMPLES" ON)
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

option(ENABLE_DOCUMENTATION "ENABLE_DOCUMENTATION" OFF)
if(ENABLE_DOCUMENTATION)
  add_subdirectory(documentation)
endif()

# todo: follow https://github.com/TheLartians/ModernCppStarter