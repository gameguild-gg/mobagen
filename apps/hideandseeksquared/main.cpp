// Hide and Seek Squared on the core app host (core-app-host plan, todo 15).
//
// The host (app::App + the SDL3 callback trampolines behind MOBAGEN_MAIN) owns
// the window, the WebGPU surface/device, the ImGui layer, the input feed and
// the frame loop; only the game (Manager + its shadow-cast grid) remains here.
// Everything the old main.cpp hand-rolled — the AllowProcessEvents adapter
// pump, per-platform surface creation, the ImGui boot, the resize
// Invalidate dance, measure_delta_seconds and the emscripten shim — is host
// territory now (app::WebGPUContext reconfigures surfaces per platform,
// including the wayland path the old Xlib-only code lacked).
//
// HeadlessNone (--mobagen-headless) has no GPU device, so the ImGuiLayer is
// not attached there (its init needs a device); Manager::Update still queries
// ImGui state (mouse pos, display size), so that mode gets a bare ImGui
// context instead — pure logic loop (flocking pattern).
#include "Manager.h"

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"

#include <imgui.h>

namespace {

struct HasApp : app::AppCallbacks {
  app::ImGuiLayer gui_layer;
  Manager manager;  // plain member: default ctor, no world/sched refs
  int smoke_frames = -1;  // --smoke-frames <N>: request exit after N iterates

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Hide and Seek Squared";
    app.settings.width = 1280;  // old SDL_CreateWindow size
    app.settings.height = 720;
    app.settings.high_pixel_density = true;  // old SDL_WINDOW_HIGH_PIXEL_DENSITY
    app.settings.clear_color[0] = 0.08f;
    app.settings.clear_color[1] = 0.08f;
    app.settings.clear_color[2] = 0.08f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);
    for (int i = 1; i + 1 < argc; ++i)
      if (SDL_strcmp(argv[i], "--smoke-frames") == 0) smoke_frames = SDL_atoi(argv[i + 1]);

    if (app.settings.render_mode == app::AppSettings::RenderMode::HeadlessNone)
      ImGui::CreateContext();
    else
      app.attach_gui(gui_layer);

    manager.Start();  // Reset() only — grid + Random, no ImGui/GPU state yet
    return SDL_APP_CONTINUE;
  }

  // ESC quits with a clean exit 0 (old tick loop: KEY_DOWN ESC -> running=false).
  SDL_AppResult on_event(app::App& app, const SDL_Event& e) override {
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
      app.request_exit();
      return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float dt) override {
    manager.Update(dt);  // host dt replaces measure_delta_seconds
    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App& app, WGPURenderPassEncoder pass) override {
    (void)app;
    (void)pass;  // Manager draws through ImGui's background draw list
    // Old tick order was OnGui -> Update -> OnDraw; the host runs Update in
    // on_iterate before the GPU frame, so a frame is now Update -> OnGui ->
    // OnDraw — same frame, GUI emit order preserved.
    manager.OnGui();
    manager.OnDraw();
  }

  void on_shutdown(app::App& app) override {
    // HeadlessNone's bare context dies here; attached layers are shut down by
    // the host after on_shutdown.
    if (app.gui() == nullptr && ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
  }
};

}  // namespace

MOBAGEN_MAIN(HasApp)
