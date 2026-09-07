#pragma once
// ============================================================================
// App — GUI-agnostic application host state; AppCallbacks — app behavior.
// ============================================================================
// The host (SDL3 callback trampolines in sdl_app.cpp, driven by app.cpp's
// host_* functions) owns the window, the WebGPU context, the input feed, an
// ecs::World and a jobs::Scheduler. An app plugs in by deriving AppCallbacks
// and registering itself with MOBAGEN_MAIN(AppT) (sdl_app.hpp). The host knows
// nothing about any GUI toolkit: GUI layers attach through the type-erased
// GuiLayerView below (implemented by the GUI layer targets of the
// core-app-host plan, todos 6/7).
//
// Lifecycle order (SDL_AppInit): app on_init(argc, argv) — settings may still
// change here, e.g. render mode/title (AppSettings::parse maps the host's
// --mobagen-headless / --mobagen-null-gpu flags), and the GUI layer is
// attached — then the per-mode startup runs:
//   Windowed     — SDL_Init(VIDEO|GAMEPAD) + content-scaled window + WebGPU
//                  surface context.
//   HeadlessNull — SDL_Init(0), no window; Dawn null-backend device
//                  (WGPUBackendType_Null) with no surface. Falls back to
//                  HeadlessNone with a logged warning when unavailable (web
//                  builds always — emdawnwebgpu has no null backend — and
//                  native builds whose Dawn lacks the null backend).
//   HeadlessNone — SDL_Init(0) only; zero GPU objects, on_draw never called.
// Finally gui->init.
//
// Frame order (SDL_AppIterate, windowed AND HeadlessNull): dt (clamped at
// 0.1 s) -> app on_iterate(dt) -> GPU frame: acquire the frame target (window
// surface texture, or HeadlessNull's cached offscreen texture — created once,
// recreated only on size change) -> render pass with the configured clear
// color -> gui->new_frame() -> app on_draw(pass) -> gui->render(pass) ->
// submit -> present (windowed only; HeadlessNull submits and ticks, nothing
// is presented) -> tick. gui->new_frame() runs BEFORE on_draw so app GUI code
// inside on_draw emits into an open GUI frame that gui->render(pass)
// submits; app logic/GUI code does NOT belong in on_iterate. HeadlessNone
// stops after on_iterate: pure logic loop, no GPU objects at all.
// Per-frame input edges are cleared AFTER each iterate (events are delivered
// between iterates through SDL_AppEvent, matching InputState's clear-before-
// feed contract).
//
// Exit paths: on_init/on_event/on_iterate returning SDL_APP_SUCCESS/FAILURE
// (exit code 0/1) or App::request_exit() stop the loop; SDL_AppQuit always
// runs afterwards, so on_shutdown and the GUI shutdown must tolerate states
// where later init stages never ran (e.g. SUCCESS straight out of on_init).
#include "app/app_settings.hpp"
#include "app/webgpu_context.hpp"
#include "ecs/world.hpp"
#include "input/input_state.hpp"
#include "jobs/scheduler.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <webgpu/webgpu.h>

struct SDL_Window;

namespace app {

  class App;  // defined below; referenced by GuiLayerView / AppCallbacks

  // Type-erased GUI layer attach point. Implemented by the GUI layer targets
  // of the core-app-host plan (todos 6/7); the host calls these hooks at the
  // right points but never includes their headers.
  struct GuiLayerView {
    virtual ~GuiLayerView() = default;
    virtual bool init(App& app) = 0;
    virtual void process_event(const SDL_Event& event) = 0;
    virtual void new_frame() = 0;
    virtual void render(WGPURenderPassEncoder pass) = 0;
    virtual void on_surface_resized() = 0;
    virtual void shutdown() = 0;
  };

  // App behavior hooks. All optional (sane empty defaults). Return
  // SDL_APP_CONTINUE to keep running; SDL_APP_SUCCESS / SDL_APP_FAILURE stop
  // the app with exit code 0 / 1 (SDL_AppQuit still runs afterwards).
  class AppCallbacks {
  public:
    virtual ~AppCallbacks() = default;

    // Top of SDL_AppInit, BEFORE any SDL_Init/window/GPU object: parse argv,
    // adjust app.settings (render mode, title, size) and attach the GUI layer
    // here. Returning SUCCESS exits 0 immediately without entering the loop
    // (CLI-only runs); FAILURE exits 1.
    virtual SDL_AppResult on_init(App& app, int argc, char** argv) {
      (void)app;
      (void)argc;
      (void)argv;
      return SDL_APP_CONTINUE;
    }

    // Every SDL event, after the GUI layer and the host input feed saw it and
    // after resize bookkeeping. Non-CONTINUE results propagate to SDL.
    virtual SDL_AppResult on_event(App& app, const SDL_Event& event) {
      (void)app;
      (void)event;
      return SDL_APP_CONTINUE;
    }

    // Once per frame with the clamped frame delta (<= 0.1 s).
    virtual SDL_AppResult on_iterate(App& app, float dt) {
      (void)app;
      (void)dt;
      return SDL_APP_CONTINUE;
    }

    // Inside the open render pass, after gui->new_frame() — called whenever a
    // GPU frame target exists: Windowed (window surface) and HeadlessNull
    // (offscreen Dawn null device). Never called in HeadlessNone.
    virtual void on_draw(App& app, WGPURenderPassEncoder pass) {
      (void)app;
      (void)pass;
    }

    // Once from SDL_AppQuit, on every exit path — even when init stopped early
    // (must tolerate a world/scheduler that never saw a frame).
    virtual void on_shutdown(App& app) { (void)app; }
  };

  // Host state, owned by the host trampolines. Apps reach it through the App&
  // every callback receives.
  class App {
  public:
    AppSettings settings;
    WebGPUContext gpu;
    input::InputState input;
    ecs::World world;
    jobs::Scheduler sched;
    SDL_Window* window = nullptr;        // null when headless or init failed
    AppCallbacks* callbacks = nullptr;   // wired by MOBAGEN_MAIN

    void attach_gui(GuiLayerView& gui) { gui_ = &gui; }
    GuiLayerView* gui() const { return gui_; }

    // Stop the app: from on_event the app stops before the next frame, from
    // on_iterate before the next iterate. `success` picks exit code 0 vs 1.
    void request_exit(bool success = true);

    WGPUDevice device() const { return gpu.device(); }  // null in HeadlessNone (and after HeadlessNull fallback)
    int width() const { return surface_width_; }        // configured surface size
    int height() const { return surface_height_; }

    // --- host-side wiring (app.cpp / sdl_app.cpp); not for app code ---------
    void set_surface_size(int width, int height);
    // Returns and clears a pending request_exit(); SDL_APP_CONTINUE if none.
    SDL_AppResult take_exit_request();

  private:
    GuiLayerView* gui_ = nullptr;
    int surface_width_ = 0;
    int surface_height_ = 0;
    SDL_AppResult pending_exit_ = SDL_APP_CONTINUE;
  };

  // Host registration (app.cpp). MOBAGEN_MAIN calls set_app from a static
  // initializer that runs before SDL_AppInit; tests may call it directly.
  void set_app(App* app);
  App* app_instance();

  // Host lifecycle drivers (app.cpp). The SDL_App* trampolines in sdl_app.cpp
  // delegate here; host tests can drive these directly without linking the
  // entry-point trampoline TU.
  SDL_AppResult host_init(App& app, int argc, char** argv);
  SDL_AppResult host_event(App& app, const SDL_Event& event);
  SDL_AppResult host_iterate(App& app);
  void host_quit(App& app);

}  // namespace app
