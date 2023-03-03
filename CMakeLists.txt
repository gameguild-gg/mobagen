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
ENDIF()

include(external/external.cmake)
add_subdirectory(core)
#add_subdirectory(engine)
#add_subdirectory(editor)
add_subdirectory(examples)

# first we can indicate the documentation build as an option and set it to ON by default
#option(BUILD_DOC "Build documentation" ON)
#IF(BUILD_DOC)
#    add_subdirectory(docs)
#ENDIF()
## check if Doxygen is installed
#find_package(Doxygen)
#if (DOXYGEN_FOUND)
#    # set input and output files
#    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/../docs/Doxyfile.in)
#    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
#
#    # request to configure the file
#    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
#    message("Doxygen build started")
#
#    # note the option ALL which allows to build the docs together with the application
#    add_custom_target( doc_doxygen ALL
#            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
#            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#            COMMENT "Generating API documentation with Doxygen"
#            VERBATIM )
#else (DOXYGEN_FOUND)
#    message("Doxygen need to be installed to generate the doxygen documentation")
#endif (DOXYGEN_FOUND)

# todo: follow https://github.com/TheLartians/ModernCppStarter