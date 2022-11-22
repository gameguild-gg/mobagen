cmake_minimum_required(VERSION 3.23.2)

# CPM downloader
include(external/cpm.cmake)

# project declaration
project(MoBaGEn)

# standands
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# todo: fix this error if the c standard is required
# error: EM_ASM does not work in -std=c* modes, use -std=gnu* modes instead
#set(CMAKE_C_STANDARD 23)
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
    #  SET(CMAKE_CXX_FLAGS "--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/ -s WASM=0 -s ALLOW_MEMORY_GROWTH=1 -s USE_SDL=2 -s DISABLE_EXCEPTION_CATCHING=2 -s USE_PTHREADS=1 --proxy-to-worker -s FETCH=1")
#    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/ -s WASM=0 -s DISABLE_EXCEPTION_CATCHING=2 --proxy-to-worker -s FETCH=1")
#    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets/")

    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -s WASM=0 -s FULL_ES2=1 -s USE_SDL=0 -s EXPORT_ALL=1")
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -s WASM=0 -s FULL_ES2=1 -s USE_SDL=0 -s EXPORT_ALL=1")

    add_definitions(-DASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/")

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/html/index.html ${CMAKE_BINARY_DIR}/bin/index.html COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/html/catchthecat.html ${CMAKE_BINARY_DIR}/bin/catchthecat.html COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/html/flocking.html ${CMAKE_BINARY_DIR}/bin/flocking.html COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/html/maze.html ${CMAKE_BINARY_DIR}/bin/maze.html COPYONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/html/scenario.html ${CMAKE_BINARY_DIR}/bin/scenario.html COPYONLY)
ENDIF()

add_subdirectory(external)
add_subdirectory(core)
add_subdirectory(examples)
add_subdirectory(editor)