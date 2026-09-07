// Maze — hosted on the core SDL3/WebGPU app host (core-app-host plan, todo 8).
// Window/GPU/ImGui boot and the frame loop live in core/sources/app; this file
// only maps the game onto the host callbacks: World::Start once in on_init,
// then Update(dt) per frame in on_iterate and OnDraw/OnGui inside the host's
// open ImGui frame in on_draw (the old hand-rolled loop's order, ex-main
// Update -> OnDraw -> OnGui).
#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"
#include "World.h"

#include <SDL3/SDL_log.h>

#include <cstdlib>
#include <cstring>

namespace {

struct MazeApp : app::AppCallbacks {
  app::ImGuiLayer imgui_layer;
  World mazeWorld{21};
  int smoke_frames = 0;  // --smoke-frames N: deterministic exit-0 headless smoke

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Maze";
    const float clear[4] = {0.05f, 0.05f, 0.05f, 1.00f};
    for (int i = 0; i < 4; ++i) app.settings.clear_color[i] = clear[i];
    app::AppSettings::parse(argc, argv, app.settings);  // --mobagen-headless / --mobagen-null-gpu
    for (int i = 1; i < argc; ++i)
      if (std::strcmp(argv[i], "--smoke-frames") == 0 && i + 1 < argc) smoke_frames = std::atoi(argv[++i]);

    // HeadlessNone has no device: ImGuiLayer::init would fail (host failure
    // path). Run its pure logic loop without a GUI layer instead.
    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) app.attach_gui(imgui_layer);

    SDL_Log("Creating Maze World");
    mazeWorld.Start();
    SDL_Log("Maze World Started");
    return SDL_APP_CONTINUE;
  }

  // Logic only: HeadlessNone never calls on_draw, so Update lives here.
  SDL_AppResult on_iterate(app::App& app, float dt) override {
    mazeWorld.Update(dt);
    if (smoke_frames > 0 && --smoke_frames == 0) {
      app.request_exit();
      return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
  }

  // GUI + background draw list, inside the ImGui frame the host opened.
  void on_draw(app::App& app, WGPURenderPassEncoder pass) override {
    (void)app;
    (void)pass;
    mazeWorld.OnDraw();
    mazeWorld.OnGui();
  }
};

}  // namespace

MOBAGEN_MAIN(MazeApp)
