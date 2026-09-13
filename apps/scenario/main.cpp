// Scenario — particles/random-field generator demo on the core app host.
//
// Hosted app (core-app-host plan, todo 9): the SDL3 callback host owns the
// window, the WebGPU context, the ImGui layer and the frame loop. Frame order
// matches the old hand-rolled main: manager.Update(dt) in on_iterate, then —
// inside the host's open render pass — manager.OnGui() + manager.OnDraw()
// (background draw list) in on_draw, submitted by the GUI layer's render.
#include "Manager.h"

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"

struct ScenarioApp : app::AppCallbacks {
  app::ImGuiLayer gui;
  Manager manager;
  int smoke_frames = 0;  // --smoke-frames N: exit after N iterates (0 = forever)

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Scenario";
    app.settings.clear_color[0] = 0.05f;
    app.settings.clear_color[1] = 0.05f;
    app.settings.clear_color[2] = 0.05f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);
    for (int i = 1; i + 1 < argc; ++i) {
      if (SDL_strcmp(argv[i], "--smoke-frames") == 0) smoke_frames = SDL_atoi(argv[i + 1]);
    }

    // HeadlessNone has no GPU device: the host never opens a render pass (or
    // calls on_draw) there, and a GUI layer that fails init aborts startup —
    // so attach the ImGui layer only when a GPU frame can actually exist.
    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) app.attach_gui(gui);

    manager.Start();
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float dt) override {
    manager.Update(dt);
    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();  // consumed next frame start
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App& app, WGPURenderPassEncoder pass) override {
    (void)app;
    (void)pass;
    manager.OnGui();
    manager.OnDraw();  // pixels into the background draw list before gui render
  }
};

MOBAGEN_MAIN(ScenarioApp)
