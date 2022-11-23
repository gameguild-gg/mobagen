cmake_minimum_required(VERSION 3.16.3)

# CPM downloader
include(external/cpm.cmake)

# project declaration
project(MoBaGEn)

# standands
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# todo: fix this error if the c standard is required
# error: EM_ASM does not work in -std=c* modes, use -std=gnu* modes instead
#set(CMAKE_C_STANDARD 17)
#set(CMAKE_C_EXTENSIONS OFF)

# set output dirs
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# set linkage
LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/libs)
LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/libs/Debug)

IF(EMSCRIPTEN)
    message(STATUS "emscripten build")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Oz -s WASM=2 -s USE_SDL=0")
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Oz -s WASM=2 -s USE_SDL=0")

    file(GLOB HTML "${CMAKE_CURRENT_SOURCE_DIR}/html/*")
    file(COPY ${HTML} DESTINATION ${CMAKE_BINARY_DIR}/bin/)
ENDIF()

add_subdirectory(external)
add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(examples)