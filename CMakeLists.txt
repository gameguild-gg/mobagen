cmake_minimum_required(VERSION 3.23.2)
include(cmake/gate/cmake/HunterGate.cmake)
option(HUNTER_STATUS_DEBUG "Hunter debug info" ON)
HunterGate(
        URL "https://github.com/cpp-pm/hunter/archive/v0.23.269.tar.gz"
        SHA1 "64024b7b95b4c86d50ae05b926814448c93a70a0"
        LOCAL
)

project(mobagen)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 17)
#set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

#CHECK MINGW
IF(NOT DEFINED MINGW)
    SET(MINGW OFF)
ENDIF()

# check if mingw
#IF("${CMAKE_GENERATOR}" MATCHES "(M|m?)in(G|g?)(W|w?)")
#    SET(MINGW ON)
#    MESSAGE(STATUS "MinGW Detected")
#    MESSAGE(STATUS "${CMAKE_GENERATOR}")
#ELSE()
#    SET(MINGW OFF CACHE BOOL "MINGW")
#ENDIF()

## make this only for VS and not WIN32
#if(DEFINED WIN32 AND NOT DEFINED MINGW)
#    SET(CMAKE_CXX_FLAGS "/w /EHsc")
#endif()

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")

# CHECK OR APPLE MACHINE
IF(NOT DEFINED APPLE)
    set(APPLE OFF)
    MESSAGE(STATUS "NOT APPLE MACHINE")
ENDIF()


find_program(LSB_RELEASE_EXEC lsb_release)
if(LSB_RELEASE_EXEC)
    execute_process(COMMAND ${LSB_RELEASE_EXEC} -is
            OUTPUT_VARIABLE LSB_RELEASE_ID_SHORT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            )
    IF(LSB_RELEASE_ID_SHORT)
        message(STATUS "ubuntu detected")
        SET(UBUNTU ON)
    ENDIF()
ENDIF()

# make this more general approach
#set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "CMAKE_POSITION_INDEPENDENT_CODE")
#IF(DEFINED UBUNTU)
#    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
#    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
#ENDIF()

MESSAGE(STATUS "MSYS=${MSYS}")
MESSAGE(STATUS "CYGWIN=${CYGWIN}")
MESSAGE(STATUS "MINGW=${MINGW}")
MESSAGE(STATUS "WIN32=${WIN32}")
MESSAGE(STATUS "MSVC=${MSVC}")
MESSAGE(STATUS "UBUNTU=${UBUNTU}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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
ENDIF()

set ( SDL2_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL/include)
# import the generated SDL2 config dir

add_subdirectory(external)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/SDL/include)
add_subdirectory(core)

#SET(CORE_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core)

add_subdirectory(editor)
add_subdirectory(examples)

#add_library(main SHARED main.cpp)
#target_sources(main PRIVATE main.cpp)
#target_link_libraries(main SDL2 ${SDL2_LIBRARY})

