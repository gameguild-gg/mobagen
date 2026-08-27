set(BUILD_SHARED_LIBS FALSE)

# ---------------------------------------------------------------------------
# SDL3 target selection Android needs the shared variant (loaded by SDLActivity via JNI). All other
# platforms use the static variant.
# ---------------------------------------------------------------------------
if(ANDROID)
  set(_CORE_SDL_TARGET SDL3::SDL3-shared)
else()
  set(_CORE_SDL_TARGET SDL3::SDL3-static)
endif()

# ---------------------------------------------------------------------------
# RmlUi — collect optional libraries if the target is available
# ---------------------------------------------------------------------------
set(RMLUI_LIBS "")
if(TARGET rmlui_all)
  list(APPEND RMLUI_LIBS rmlui_all)
  message(STATUS "RmlUi linked to core")
endif()

# ---------------------------------------------------------------------------
# 10 DOD subsystems (each defines their own mobagen::core_* target)
# ---------------------------------------------------------------------------
add_subdirectory(sources/ecs)
add_subdirectory(sources/jobs)
add_subdirectory(sources/reactive)
add_subdirectory(sources/messaging)
add_subdirectory(sources/scene)
add_subdirectory(sources/camera)
add_subdirectory(sources/input)
add_subdirectory(sources/render)
add_subdirectory(sources/resource)
add_subdirectory(sources/net)

# ---------------------------------------------------------------------------
# Auxiliary subsystems
# ---------------------------------------------------------------------------
add_subdirectory(sources/datastructures)
add_subdirectory(sources/error)

option(BUILD_SCRIPT "Build script subsystem" OFF)
if(BUILD_SCRIPT)
  add_subdirectory(sources/script)
endif()

# WebRequest — source files live directly in sources/ (not a subdirectory). Build as a standalone
# static library when opted in.
option(BUILD_WEBREQUEST "Build WebRequest subsystem" OFF)
if(BUILD_WEBREQUEST)
  add_library(core_webrequest STATIC sources/WebRequest.cpp sources/WebRequest.h)
  add_library(mobagen::core_webrequest ALIAS core_webrequest)
  target_include_directories(core_webrequest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sources)
endif()

# android_main_wrapper.cpp is intentionally NOT compiled here. It is injected by the root
# CMakeLists.txt Android shim to avoid double-linking the SDL_main symbol.

# Random.h / Random.cpp — standalone static helper, NOT part of the umbrella. Link
# mobagen::core_random directly if needed.
add_library(core_random STATIC sources/Random.cpp sources/Random.h)
add_library(mobagen::core_random ALIAS core_random)
target_include_directories(core_random PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sources)

# ---------------------------------------------------------------------------
# Umbrella INTERFACE target — backward-compatible alias for all consumers
# ---------------------------------------------------------------------------
add_library(core INTERFACE)
add_library(mobagen::core ALIAS core)

target_link_libraries(
  core
  INTERFACE mobagen::core_ecs
            mobagen::core_jobs
            mobagen::core_reactive
            mobagen::core_messaging
            mobagen::core_scene
            mobagen::core_camera
            mobagen::core_input
            mobagen::core_render
            mobagen::core_resource
            mobagen::core_net
            ${_CORE_SDL_TARGET}
            SDL3_image::SDL3_image-static
            IMGUI
            dawn::webgpu
            ${RMLUI_LIBS}
)

target_include_directories(
  core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sources
)

set(CORE_INC_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/sources"
    CACHE STRING "CORE INCLUDE SOURCE PATH"
)

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
install(TARGETS core DESTINATION lib)

file(
  GLOB_RECURSE CORE_INC CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.h *.hpp
)
install(FILES ${CORE_INC} DESTINATION include/core)
