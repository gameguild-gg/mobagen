#execute_process( COMMAND git submodule update --init --recursive )
#set_directory_properties( PROPERTIES CLEAN_NO_CUSTOM 1)

if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

IF (EMSCRIPTEN)
ELSEIF (ANDROID)
    #  set( SDL_STATIC ON CACHE BOOL "Build the static SDL library" )
    #  set( SDL_SHARED OFF CACHE BOOL "Build the shared SDL library" )
    #  # set( SDL_FILESYSTEM FALSE )
    #  set( PTHREADS OFF CACHE BOOL "Pthread support" )
    #  add_definitions(-DGL_GLEXT_PROTOTYPES)
ELSE ()
    #  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/)
    #  IF (WIN32)
    #    add_library (whereami STATIC ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.c ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.h)
    #  ELSE ()
    #    add_library (whereami SHARED ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.c ${CMAKE_CURRENT_SOURCE_DIR}/whereami/src/whereami.h)
    #  ENDIF ()
#    set( SDL_STATIC OFF CACHE BOOL "Build the static SDL library" )
#    set( SDL_SHARED ON CACHE BOOL "Build the shared SDL library" )

    #  set( glew-cmake_BUILD_SHARED ON CACHE BOOL "Build the shared glew library" )
    #  set( glew-cmake_BUILD_STATIC OFF CACHE BOOL "Build the static glew library" )
    #  set( glew-cmake_BUILD_SINGLE_CONTEXT ON CACHE BOOL "Build the single context glew library" )
    #  set( glew-cmake_BUILD_MULTI_CONTEXT OFF CACHE BOOL "Build the multi context glew library" )
    #
    #  add_subdirectory( glew )
ENDIF ()

set(SDL_SHARED OFF CACHE BOOL "SDL_SHARED")
SET(SDL_STATIC ON CACHE BOOL "SDL_STATIC")
SET(SDL_STATIC_PIC ON CACHE BOOL "SDL_STATIC_PIC")

add_definitions(-DHAVE_LIBC=OFF)

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

#ImGui
CPMAddPackage(
        NAME IMGUI
        VERSION 1.88
        URL "https://github.com/ocornut/imgui/archive/refs/tags/v1.88.zip"
)
IF(IMGUI_ADDED)
    add_library(IMGUI STATIC)

    target_sources( IMGUI
            PRIVATE
            ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
            ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
            ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
            ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
            ${IMGUI_SOURCE_DIR}/imgui.cpp

            PRIVATE
            ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer.cpp
            ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl.cpp
            )
    target_include_directories( IMGUI
            PUBLIC ${IMGUI_SOURCE_DIR}
            PUBLIC ${IMGUI_SOURCE_DIR}/backends
            PUBLIC ${SDL2_DIR}/include
            )

    target_link_libraries(IMGUI PUBLIC SDL2-static ${CMAKE_DL_LIBS})
ENDIF()

#set( ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Build assimp tools" )
#set( ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build assimp tests" )
#set( ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Build assimp samples" )
#
#set( ASSIMP_BUILD_3DS_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_3DS_EXPORTER)
#
#set( ASSIMP_BUILD_AC_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_AC_EXPORTER)
#
#set( ASSIMP_BUILD_ASE_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_ASE_EXPORTER)
#
#set( ASSIMP_BUILD_ASSBIN_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_ASSBIN_EXPORTER)
#
#set( ASSIMP_BUILD_ASSXML_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_ASSXML_EXPORTER)
#
#set( ASSIMP_BUILD_B3D_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_B3D_EXPORTER)
#
#set( ASSIMP_BUILD_BVH_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_BVH_EXPORTER)
#
#set( ASSIMP_BUILD_COLLADA_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_COLLADA_EXPORTER)
#
#set( ASSIMP_BUILD_DXF_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_DXF_EXPORTER)
#
#set( ASSIMP_BUILD_CSM_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_CSM_EXPORTER)
#
#set( ASSIMP_BUILD_HMP_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_HMP_EXPORTER)
#
#set( ASSIMP_BUILD_IRRMESH_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_IRRMESH_EXPORTER)
#
#set( ASSIMP_BUILD_IRR_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_IRR_EXPORTER)
#
#set( ASSIMP_BUILD_LWO_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_LWO_EXPORTER)
#
#set( ASSIMP_BUILD_LWS_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_LWS_EXPORTER)
#
#set( ASSIMP_BUILD_MD2_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_MD2_EXPORTER)
#
#set( ASSIMP_BUILD_MD3_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_MD3_EXPORTER)
#
#set( ASSIMP_BUILD_MD5_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_MD5_EXPORTER)
#
#set( ASSIMP_BUILD_MDC_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_MDC_EXPORTER)
#
#set( ASSIMP_BUILD_MDL_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_MDL_EXPORTER)
#
#set( ASSIMP_BUILD_NFF_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_NFF_EXPORTER)
#
#set( ASSIMP_BUILD_NDO_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_NDO_EXPORTER)
#
#set( ASSIMP_BUILD_OFF_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_OFF_EXPORTER)
#
#set( ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_OBJ_EXPORTER)
#
#set( ASSIMP_BUILD_OGRE_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_OGRE_EXPORTER)
#
#set( ASSIMP_BUILD_OPENGEX_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_OPENGEX_EXPORTER)
#
#set( ASSIMP_BUILD_PLY_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_PLY_EXPORTER)
#
#set( ASSIMP_BUILD_MS3D_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_MS3D_EXPORTER)
#
#set( ASSIMP_BUILD_COB_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_COB_EXPORTER)
#
#set( ASSIMP_BUILD_BLEND_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_BLEND_EXPORTER)
#
#set( ASSIMP_BUILD_IFC_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_IFC_EXPORTER)
#
#set( ASSIMP_BUILD_XGL_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_XGL_EXPORTER)
#
#set( ASSIMP_BUILD_FBX_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_FBX_EXPORTER)
#
#set( ASSIMP_BUILD_Q3D_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_Q3D_EXPORTER)
#
#set( ASSIMP_BUILD_Q3BSP_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_Q3BSP_EXPORTER)
#
#set( ASSIMP_BUILD_RAW_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_RAW_EXPORTER)
#
#set( ASSIMP_BUILD_SIB_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_SIB_EXPORTER)
#
#set( ASSIMP_BUILD_SMD_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_SMD_EXPORTER)
#
#set( ASSIMP_BUILD_STL_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_STL_EXPORTER)
#
#set( ASSIMP_BUILD_TERRAGEN_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_TERRAGEN_EXPORTER)
#
#set( ASSIMP_BUILD_3D_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_3D_EXPORTER)
#
#set( ASSIMP_BUILD_X_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_XFILE_EXPORTER)
#
#set( ASSIMP_BUILD_GLTF_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_GLTF_EXPORTER)
#
#set( ASSIMP_BUILD_3MF_IMPORTER OFF CACHE BOOL "Build importer" )
#add_definitions(-DASSIMP_BUILD_NO_3MF_EXPORTER)
#
#add_subdirectory( assimp )
#
#INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/imgui/)
#IF (WIN32)
#  add_library (imgui STATIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp)
#ELSE ()
#  add_library (imgui SHARED ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp)
#ENDIF ()

#set(USE_MSVC_RUNTIME_LIBRARY_DLL ON)
#set(BUILD_SHARED_LIBS OFF)
#set(BUILD_UNIT_TESTS OFF CACHE BOOL "Do not build unit tests for bullet.")
#set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "no bullet2 demos.")
#set(BUILD_CPU_DEMOS OFF CACHE BOOL "no bullet demos.")
#set(BUILD_BULLET3 OFF CACHE BOOL "no bullet 3.")
#add_subdirectory(bullet3)