set(BUILD_SHARED_LIBS FALSE)

file(
  GLOB_RECURSE CORE_INC CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.h *.hpp
)

file(
  GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.c *.cpp
)

set(CORE_INC_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/sources"
    CACHE STRING "CORE INCLUDE SOURCE PATH"
)

add_library(core STATIC ${CORE_SRC} ${CORE_INC})

# Expose both core/ root and core/sources/ so consumers can include headers
# from either path without having to know the internal layout.
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sources
)

# Collect optional RmlUi libraries if available
set(RMLUI_LIBS "")
if(TARGET rmlui_all)
  list(APPEND RMLUI_LIBS rmlui_all)
  message(STATUS "RmlUi linked to core")
endif()

# IMGUI already PUBLIC-links SDL3::SDL3-static + dawn::webgpu, so consumers
# of `core` get SDL3, WebGPU (Dawn / emdawnwebgpu) and ImGui transitively.
# IMGUI already PUBLIC-links SDL3 + dawn::webgpu, so consumers of `core`
# get SDL3, WebGPU (Dawn / emdawnwebgpu) and ImGui transitively.
# Android needs the shared SDL3 (loaded by SDLActivity via JNI); all
# other platforms use the static variant.
if(ANDROID)
  set(_CORE_SDL_TARGET SDL3::SDL3-shared)
else()
  set(_CORE_SDL_TARGET SDL3::SDL3-static)
endif()

target_link_libraries(core
  PUBLIC
    ${_CORE_SDL_TARGET}
    SDL3_image::SDL3_image-static
    IMGUI
    dawn::webgpu
    ${RMLUI_LIBS}
)

# If(NOT EMSCRIPTEN AND NOT ANDROID) add_definitions(-DGLEW_STATIC) #  add_definitions(-DGLEW_BUILD)
# set(GLEW_USE_STATIC_LIBS ON CACHE BOOL "bla") set(BUILD_SHARED_LIBS OFF CACHE BOOL "bla") set(
# glew-cmake_BUILD_SHARED OFF CACHE BOOL "Build the shared glew library" ) set(
# glew-cmake_BUILD_STATIC ON CACHE BOOL "Build the static glew library" ) set(
# glew-cmake_BUILD_SINGLE_CONTEXT ON CACHE BOOL "Build the single context glew library" ) set(
# glew-cmake_BUILD_MULTI_CONTEXT OFF CACHE BOOL "Build the multi context glew library" )
#
# message(STATUS "######################### ADDING GLEW #######################################")
# hunter_add_package(glew) find_package(glew CONFIG REQUIRED)
#
# target_link_libraries (engine PUBLIC ${ENGINE_LIBS} glew::glew) ELSE() target_link_libraries
# (engine PUBLIC ${ENGINE_LIBS}) ENDIF()

# TODO: Check if this approach bellow is better IF (ANDROID) add_library (core STATIC ${CORE_SRC})
# ELSE () add_library (core SHARED ${CORE_SRC}) ENDIF ()
#
# target_link_libraries (core ${ENGINE_LIBS}) set_target_properties(core PROPERTIES
# COTIRE_CXX_PREFIX_HEADER_INIT "stdafx.h") cotire(core)

install(TARGETS core DESTINATION lib)
install(FILES ${CORE_INC} DESTINATION include/core)

# TODO: ADD THIS BACK WHEN NEEDED IF (NOT TARGET uninstall) # add make uninstall capability
# configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/cmake_uninstall.cmake.in"
# "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY) add_custom_target(uninstall
# "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake") ENDIF()

# include_directories(${SDL2_INCLUDE_DIR} core)

option(ENABLE_CURL "ENABLE_WEB" ON)
if(ENABLE_CURL)

endif()
