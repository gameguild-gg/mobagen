set(BUILD_SHARED_LIBS FALSE)

file(GLOB CORE_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

file(GLOB CORE_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        )

SET(CORE_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "CORE INCLUDE SOURCE PATH")

#SET(CORE_SRC ${CORE_SRC} ${CORE_INC})

add_library(core STATIC ${CORE_SRC} ${CORE_INC})
target_link_libraries (core PUBLIC SDL2-static IMGUI)

IF(EMSCRIPTEN)
    target_compile_options(core PUBLIC "-O0")
    target_link_options(core PUBLIC "-s WASM=0 -s FULL_ES2=1 -s USE_SDL=0 -s EXPORT_ALL=1")
ENDIF()
#If(NOT EMSCRIPTEN AND NOT ANDROID)
#    add_definitions(-DGLEW_STATIC)
#    #  add_definitions(-DGLEW_BUILD)
#    set(GLEW_USE_STATIC_LIBS ON CACHE BOOL "bla")
#    set(BUILD_SHARED_LIBS OFF CACHE BOOL "bla")
#    set( glew-cmake_BUILD_SHARED OFF CACHE BOOL "Build the shared glew library" )
#    set( glew-cmake_BUILD_STATIC ON CACHE BOOL "Build the static glew library" )
#    set( glew-cmake_BUILD_SINGLE_CONTEXT ON CACHE BOOL "Build the single context glew library" )
#    set( glew-cmake_BUILD_MULTI_CONTEXT OFF CACHE BOOL "Build the multi context glew library" )
#
#    message(STATUS "######################### ADDING GLEW #######################################")
#    hunter_add_package(glew)
#    find_package(glew CONFIG REQUIRED)
#
#    target_link_libraries (engine PUBLIC ${ENGINE_LIBS} glew::glew)
#ELSE()
#    target_link_libraries (engine PUBLIC ${ENGINE_LIBS})
#ENDIF()

# TODO: Check if this approach bellow is better
#IF (ANDROID)
#  add_library (core STATIC ${CORE_SRC})
#ELSE ()
#  add_library (core SHARED ${CORE_SRC})
#ENDIF ()
#
#target_link_libraries (core ${ENGINE_LIBS})
#set_target_properties(core PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT "stdafx.h")
#cotire(core)

install (TARGETS core DESTINATION lib)
install (FILES ${CORE_INC} DESTINATION include/core)

# TODO: ADD THIS BACK WHEN NEEDED
# IF (NOT TARGET uninstall)
#   # add make uninstall capability
#   configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/cmake_uninstall.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)
#   add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
# ENDIF()

include_directories(${SDL2_INCLUDE_DIR} core)