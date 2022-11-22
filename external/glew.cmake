# GLEW
string(TIMESTAMP BEFORE "%s")
# todo: move this to cpm options
set( ONLY_LIBS ON CACHE BOOL "cmake_ONLY_LIBS" )
set( glew-cmake_BUILD_SHARED OFF CACHE BOOL "Build the shared glew library" )
set( glew-cmake_BUILD_STATIC ON CACHE BOOL "Build the static glew library" )
set( glew-cmake_BUILD_SINGLE_CONTEXT ON CACHE BOOL "Build the single context glew library" )
set( glew-cmake_BUILD_MULTI_CONTEXT OFF CACHE BOOL "Build the multi context glew library" )
CPMAddPackage(
        NAME GLEW
        GITHUB_REPOSITORY Perlmint/glew-cmake
        GIT_TAG glew-cmake-2.2.0
)
include_directories(${GLEW_SOURCE_DIR}/include)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAGLEW "${AFTER} - ${BEFORE}")
MESSAGE(STATUS "GLEW TIME: ${DELTAGLEW}s")