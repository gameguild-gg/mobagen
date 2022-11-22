set(SDL_SHARED OFF CACHE BOOL "SDL_SHARED")
SET(SDL_STATIC ON CACHE BOOL "SDL_STATIC")
SET(SDL_STATIC_PIC ON CACHE BOOL "SDL_STATIC_PIC")

# SDL2
CPMAddPackage(
        NAME SDL2
        VERSION 2.24.2
        URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.24.2.zip"
        OPTIONS "SDL2_DISABLE_INSTALL TRUE"
)
find_package(SDL2 REQUIRED)
if (SDL2_ADDED)
    file(GLOB SDL_HEADERS "${SDL_SOURCE_DIR}/include/*.h")

    # Create a target that copies headers at build time, when they change
    add_custom_target(sdl_copy_headers_in_build_dir
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${SDL_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/SDLHeaders/SDL2"
            DEPENDS ${SDL_HEADERS})

    # Make SDL depend from it
    add_dependencies(SDL2 sdl_copy_headers_in_build_dir)

    # And add the directory where headers have been copied as an interface include dir
    target_include_directories(SDL2 INTERFACE "${CMAKE_BINARY_DIR}/SDLHeaders")

    set (SDL2_INCLUDE_DIR ${SDL2_SOURCE_DIR}/include)
endif()
include_directories(${SDL2_INCLUDE_DIR})