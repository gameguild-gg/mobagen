// Conway's Game of Life on the core app host (core-app-host plan, todo 10).
//
// The host (app::App + the SDL3 callback trampolines behind MOBAGEN_MAIN) owns
// the window, the WebGPU context, the ImGui layer, the input feed and the frame
// loop; this file only wires the game's Manager into the lifecycle hooks:
//   on_init    — settings (title / clear color), CLI parsing, GUI layer attach,
//                Manager::Start
//   on_iterate — Manager::Update(dt), smoke-frame countdown
//   on_draw    — Manager::OnGui() + Manager::OnDraw() inside the host's open
//                GUI frame + render pass (old per-frame order preserved:
//                Update -> OnGui -> OnDraw, submitted by the host's render).
// HeadlessNone (--mobagen-headless) never creates a GPU device, so the GUI
// layer is not attached there (its init needs a device; on_draw never runs).
#include "Manager.h"

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"

#include <SDL3/SDL_log.h>

#include <cstdlib>
#include <cstring>

namespace {

struct LifeApp : app::AppCallbacks {
  app::ImGuiLayer gui_layer;
  Manager manager;
  int smoke_frames = -1;  // --smoke-frames <N>: request exit after N iterates

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Conway's Game of Life";
    app.settings.clear_color[0] = 0.05f;
    app.settings.clear_color[1] = 0.05f;
    app.settings.clear_color[2] = 0.05f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);
    for (int i = 1; i + 1 < argc; ++i)
      if (std::strcmp(argv[i], "--smoke-frames") == 0) smoke_frames = std::atoi(argv[i + 1]);

    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) app.attach_gui(gui_layer);

    manager.Start();
    SDL_Log("Game of Life Started");
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float dt) override {
    manager.Update(dt);
    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App& app, WGPURenderPassEncoder pass) override {
    (void)app;
    (void)pass;  // Manager draws through ImGui's background draw list
    manager.OnGui();
    manager.OnDraw();
  }
};

}  // namespace

MOBAGEN_MAIN(LifeApp)
