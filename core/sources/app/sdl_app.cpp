// ============================================================================
// sdl_app — the SDL3 callback trampolines. THE only TU in core including the
// SDL3 main header.
// ============================================================================
// Including it here with SDL_MAIN_USE_CALLBACKS synthesizes the platform entry
// point in this translation unit: a plain main() that runs
// SDL_RunApp -> SDL_main -> SDL_EnterAppMainCallbacks on desktop, the browser
// main loop on emscripten, and the exported SDL_main path on Android. Apps
// link mobagen::core_app, write MOBAGEN_MAIN(AppT) and never define main().
//
// The trampolines are one-line delegates to the host drivers in app.cpp
// (app::host_init/host_event/host_iterate/host_quit), which hold the real
// lifecycle logic — host tests drive those directly without pulling in this
// TU's synthesized entry point.
#include "app/sdl_app.hpp"

// Keep the SDL3 main header LAST: with SDL_MAIN_USE_CALLBACKS it ends with
// `#define main SDL_main`, and the declarations below need to precede it.
// It pulls SDL_stdinc/error/events/init but not SDL_log.h (SDL_Log).
#include <SDL3/SDL_log.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

extern "C" SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
  app::App* app = app::app_instance();
  if (app == nullptr) {
    SDL_Log("No app registered: use MOBAGEN_MAIN(AppT) (app/sdl_app.hpp)");
    return SDL_APP_FAILURE;
  }
  if (appstate != nullptr) *appstate = app;
  return app::host_init(*app, argc, argv);
}

extern "C" SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
  if (event == nullptr) return SDL_APP_CONTINUE;
  app::App* app = appstate != nullptr ? static_cast<app::App*>(appstate) : app::app_instance();
  if (app == nullptr) return SDL_APP_CONTINUE;
  return app::host_event(*app, *event);
}

extern "C" SDL_AppResult SDL_AppIterate(void* appstate) {
  app::App* app = appstate != nullptr ? static_cast<app::App*>(appstate) : app::app_instance();
  if (app == nullptr) return SDL_APP_FAILURE;
  return app::host_iterate(*app);
}

extern "C" void SDL_AppQuit(void* appstate, SDL_AppResult result) {
  (void)result;  // host_quit is result-agnostic (single shutdown sequence)
  app::App* app = appstate != nullptr ? static_cast<app::App*>(appstate) : app::app_instance();
  if (app != nullptr) app::host_quit(*app);
}
