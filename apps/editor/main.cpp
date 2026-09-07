// ============================================================================
// Editor — core app host migration (core-app-host plan, todo 13).
// ============================================================================
// The old hand-rolled SDL/Dawn/ImGui loop was a pure ImGui host loop; the host
// (app::App + MOBAGEN_MAIN, core/sources/app) now owns window/GUI/frame
// lifetime. The editor's only unique content is the TopBar GUI.
#include "TopBar.h"

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"

struct EditorApp : app::AppCallbacks {
  TopBar topbar;
  int smoke_frames = 0;  // --smoke-frames N: exit after N iterates (0 = off)

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Editor";
    // Old editor clear color (main.cpp:213).
    app.settings.clear_color[0] = 0.10f;
    app.settings.clear_color[1] = 0.10f;
    app.settings.clear_color[2] = 0.10f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);

    for (int i = 1; i < argc - 1; ++i)
      if (SDL_strcmp(argv[i], "--smoke-frames") == 0) smoke_frames = SDL_atoi(argv[i + 1]);

    // HeadlessNone is a pure logic loop: no device, no GUI frame — attaching
    // the ImGui layer there only fails its init and kills the run.
    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) {
      static app::ImGuiLayer imgui_layer;
      app.attach_gui(imgui_layer);
    }
    return SDL_APP_CONTINUE;
  }

  // The old editor loop had no per-frame logic beyond the GUI.
  SDL_AppResult on_iterate(app::App& app, float /*dt*/) override {
    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();
    return SDL_APP_CONTINUE;
  }

  // Old placement: inside the ImGui frame, before ImGui::Render (main.cpp:251).
  void on_draw(app::App& /*app*/, WGPURenderPassEncoder /*pass*/) override { topbar.render_ui(); }
};

MOBAGEN_MAIN(EditorApp)
