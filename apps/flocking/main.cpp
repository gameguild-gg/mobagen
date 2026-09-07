// Flocking demo, hosted on the core app host (app::App + SDL3 callback
// trampolines). The host owns the window, the WebGPU context and the ImGui
// layer; FlockingManager consumes the host-owned ecs::World/jobs::Scheduler
// pair (app.world / app.sched).
//
// Two ordering facts drive the shape:
//  - on_init runs before the ImGui layer exists, but FlockingManager::Start()
//    touches ImGui state (custom style, GetIO) — so Start() is deferred to the
//    first on_iterate, after the host finished gui->init.
//  - HeadlessNone has no GPU device: an attached ImGuiLayer would fail init
//    and the host would exit FAILURE. The manager still queries ImGui state
//    (display size, key states), so that mode gets a bare ImGui context and
//    no GUI layer instead — pure logic loop.
#include "app/sdl_app.hpp"
#include "gameobjects/World.h"
#include "imgui/imgui_layer.hpp"

#include <imgui.h>

#include <memory>

namespace {

struct FlockingApp : app::AppCallbacks {
  std::unique_ptr<FlockingManager> manager;
  int smoke_frames = -1;  // --smoke-frames <N>: request exit after N iterates
  bool started = false;

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Flocking";
    app.settings.clear_color[0] = 0.05f;
    app.settings.clear_color[1] = 0.05f;
    app.settings.clear_color[2] = 0.05f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);
    for (int i = 1; i + 1 < argc; ++i) {
      if (SDL_strcmp(argv[i], "--smoke-frames") == 0) smoke_frames = SDL_atoi(argv[i + 1]);
    }

    if (app.settings.render_mode == app::AppSettings::RenderMode::HeadlessNone) {
      ImGui::CreateContext();
    } else {
      static app::ImGuiLayer gui;
      app.attach_gui(gui);
    }

    manager = std::make_unique<FlockingManager>(app.world, app.sched);
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float dt) override {
    if (!started) {
      manager->Start();
      started = true;
    }
    manager->Update(dt);
    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App& app, WGPURenderPassEncoder pass) override {
    (void)app;
    (void)pass;
    manager->OnGui();
    manager->OnDraw();
  }

  void on_shutdown(app::App& app) override {
    manager.reset();
    if (app.gui() == nullptr && ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();
  }
};

}  // namespace

MOBAGEN_MAIN(FlockingApp)
