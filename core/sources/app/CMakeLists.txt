# app module — GUI-agnostic application host building blocks.
#
# webgpu_context: owns the per-app WebGPU instance/adapter/device/queue and the
# platform surface (cocoa/wayland/x11/win32 native, "#canvas" on the web).
# The SDL3 callback host (app.hpp/sdl_app.hpp) lands on top of it in later
# steps of the core-app-host plan.
#
# Fine-grained target by design: consumers link mobagen::core_app directly; the
# `core` umbrella is wired separately.

# SDL3 target selection mirrors core/CMakeLists.txt: Android loads the shared
# library via SDLActivity/JNI; every other platform links the static variant.
if(ANDROID)
  set(_APP_SDL_TARGET SDL3::SDL3-share)
else()
  set(_APP_SDL_TARGET SDL3::SDL3-static)
endif()

add_library(mobagen_core_app STATIC webgpu_context.cpp)
add_library(mobagen::core_app ALIAS mobagen_core_app)
# Include root is core/sources so consumers include "app/webgpu_context.hpp".
target_include_directories(mobagen_core_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_app PUBLIC cxx_std_23)
# dawn::webgpu is PUBLIC: webgpu_context.hpp includes <webgpu/webgpu.h>.
target_link_libraries(
  mobagen_core_app
  PUBLIC dawn::webgpu
  PRIVATE ${_APP_SDL_TARGET}
)
