#pragma once
// ============================================================================
// MOBAGEN_MAIN — register the app's callback object with the SDL3 host.
// ============================================================================
// Usage (whole app in one file; no main() anywhere in app code):
//
//   #include "app/sdl_app.hpp"
//   struct MyGame : app::AppCallbacks { /* on_init / on_iterate / on_draw */ };
//   MOBAGEN_MAIN(MyGame)
//
// The macro plants a static initializer in the app's TU that constructs the
// host app::App plus one AppT and registers both via app::set_app(). Dynamic
// initialization completes before SDL_AppInit can possibly run (SDL's entry
// point comes from main()/the platform launcher, which always runs after
// static init), and the registrar touches nothing but function-local statics
// and the stored pointer — so static-init order is safe. The platform entry
// point itself (plain main on desktop, the emscripten browser main loop, the
// exported SDL_main path on Android) is synthesized by SDL3 from sdl_app.cpp's
// single include of the SDL3 main header; nothing here includes it.
#include "app/app.hpp"

/// Instantiate + register AppT (an app::AppCallbacks subclass) with the host.
#define MOBAGEN_MAIN(AppT)                            \
  namespace {                                         \
  struct MobagenAppRegistrar {                        \
    MobagenAppRegistrar() {                           \
      static ::app::App mobagen_host;                 \
      static AppT mobagen_callbacks;                  \
      mobagen_host.callbacks = &mobagen_callbacks;    \
      ::app::set_app(&mobagen_host);                  \
    }                                                 \
  };                                                  \
  static const MobagenAppRegistrar mobagen_app_registrar; \
  }
