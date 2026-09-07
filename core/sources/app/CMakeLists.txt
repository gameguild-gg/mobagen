# app module — GUI-agnostic application host building blocks.
#
# webgpu_context: owns the per-app WebGPU instance/adapter/device/queue and the
# platform surface (cocoa/wayland/x11/win32 native, "#canvas" on the web).
# app + sdl_app: the SDL3 callback Application host — App/AppCallbacks/
# AppSettings/GuiLayerView (app.hpp) plus the four SDL_App* trampolines that
# synthesize the platform entry point (sdl_app.cpp is the ONLY TU including
# the SDL3 main header; apps register via MOBAGEN_MAIN and never write main()).
#
# Fine-grained target by design: consumers link mobagen::core_app directly; the
# `core` umbrella is wired separately.
#
# NOTE on entry-point links: the synthesized main() lives in sdl_app.o inside
# this static archive. Binaries that define their own main() simply never pull
# that member in; binaries that don't (hosted apps) get their entry point from
# the archive automatically.

# SDL3 target selection mirrors core/CMakeLists.txt: Android loads the shared
# library via SDLActivity/JNI; every other platform links the static variant.
if(ANDROID)
  set(_APP_SDL_TARGET SDL3::SDL3-shared)
else()
  set(_APP_SDL_TARGET SDL3::SDL3-static)
endif()

add_library(mobagen_core_app STATIC webgpu_context.cpp app.cpp sdl_app.cpp)
add_library(mobagen::core_app ALIAS mobagen_core_app)
# Include root is core/sources so consumers include "app/webgpu_context.hpp".
target_include_directories(mobagen_core_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_app PUBLIC cxx_std_23)
# dawn::webgpu, SDL3, ecs/jobs/input are all PUBLIC: app.hpp — the target's
# public interface — includes <webgpu/webgpu.h>, <SDL3/SDL_events.h>/
# <SDL3/SDL_init.h> (SDL_AppResult/SDL_Event in AppCallbacks), and the
# ecs/jobs/input headers (App owns a World, a Scheduler and an InputState).
# Threads arrives transitively through mobagen::core_jobs.
target_link_libraries(
  mobagen_core_app
  PUBLIC dawn::webgpu ${_APP_SDL_TARGET} mobagen::core_ecs mobagen::core_jobs mobagen::core_input
)
