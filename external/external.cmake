if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

if(EMSCRIPTEN)

elseif(ANDROID)
  # set( SDL_STATIC ON CACHE BOOL "Build the static SDL library" ) set( SDL_SHARED OFF CACHE BOOL
  # "Build the shared SDL library" ) # set( SDL_FILESYSTEM FALSE ) set( PTHREADS OFF CACHE BOOL
  # "Pthread support" ) add_definitions(-DGL_GLEXT_PROTOTYPES)
else()
  # INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/) IF (WIN32) add_library (whereami
  # STATIC ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.c
  # ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.h) ELSE () add_library (whereami SHARED
  # ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.c
  # ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.h) ENDIF () set( SDL_STATIC OFF CACHE BOOL
  # "Build the static SDL library" ) set( SDL_SHARED ON CACHE BOOL "Build the shared SDL library" )

  # set( glew-cmake_BUILD_SHARED ON CACHE BOOL "Build the shared glew library" ) set(
  # glew-cmake_BUILD_STATIC OFF CACHE BOOL "Build the static glew library" ) set(
  # glew-cmake_BUILD_SINGLE_CONTEXT ON CACHE BOOL "Build the single context glew library" ) set(
  # glew-cmake_BUILD_MULTI_CONTEXT OFF CACHE BOOL "Build the multi context glew library" )
  #
  # add_subdirectory( glew )
endif()

# include(filament.cmake) include(threadpool.cmake)
include(external/sdl.cmake)
# include(glm.cmake) include(glew.cmake)
include(external/imgui.cmake)
# include(assimp.cmake) include(bullet.cmake)
