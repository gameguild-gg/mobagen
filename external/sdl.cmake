set(SDL_SHARED
    OFF
    CACHE BOOL "SDL_SHARED"
)
set(SDL_STATIC
    ON
    CACHE BOOL "SDL_STATIC"
)
set(SDL_STATIC_PIC
    ON
    CACHE BOOL "SDL_STATIC_PIC"
)

if(NOT DEFINED EMSCRIPTEN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong") # required for opus
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong") # required for opus
endif()

# SDL2
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME SDL2
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_TAG release-2.32.8
  OPTIONS "SDL2_DISABLE_INSTALL ON"
          "SDL2_SHARED OFF"
          "SDL_SHARED OFF"
          "SDL_STATIC ON"
          "SDL_STATIC_PIC ON"
          "SDL_WERROR OFF"
          "SDL_TEST OFF"
)
find_package(SDL2 REQUIRED)
if(SDL2_ADDED)
  file(GLOB SDL_HEADERS "${SDL_SOURCE_DIR}/include/*.h")

  # Create a target that copies headers at build time, when they change
  add_custom_target(
    sdl_copy_headers_in_build_dir
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDL_SOURCE_DIR}/include"
            "${CMAKE_BINARY_DIR}/SDLHeaders/SDL2"
    DEPENDS ${SDL_HEADERS}
  )

  # Make SDL depend from it
  add_dependencies(SDL2 sdl_copy_headers_in_build_dir)

  # And add the directory where headers have been copied as an interface include dir
  target_include_directories(SDL2 INTERFACE "${CMAKE_BINARY_DIR}/SDLHeaders")

  set(SDL2_INCLUDE_DIR ${SDL2_SOURCE_DIR}/include)
endif()
include_directories(${SDL2_INCLUDE_DIR})
string(TIMESTAMP AFTER "%s")
math(EXPR DELTASDL "${AFTER} - ${BEFORE}")
message(STATUS "SDL2 TIME: ${DELTASDL}s")

# SDL_image
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME SDL_image
  GITHUB_REPOSITORY libsdl-org/SDL_image
  GIT_TAG release-2.8.8
  OPTIONS "BUILD_SHARED_LIBS OFF"
          "SDL2_SHARED OFF"
          "SDL_SHARED OFF"
          "SDL2IMAGE_INSTALL OFF"
          "SDL2IMAGE_SAMPLES OFF"
          "SDL2IMAGE_VENDORED OFF"
          "SDL2IMAGE_BUILD_SHARED_LIBS OFF"
          "SDL2IMAGE_DEPS_SHARED OFF"
)
if(SDL_image_ADDED)
  include_directories(${SDL_image_SOURCE_DIR}/include)
else()
  message(FATAL_ERROR "SDL IMAGE NOT FOUND")
endif()

string(TIMESTAMP AFTER "%s")
math(EXPR DELTASDL_image "${AFTER} - ${BEFORE}")
message(STATUS "SDL_image TIME: ${DELTASDL_image}s")
