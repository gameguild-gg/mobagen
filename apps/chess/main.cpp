// Chess on the core app host (core-app-host plan, todo 12).
//
// Host frame order maps 1:1 onto the old hand-rolled loop: manager.Update
// (logic) runs in on_iterate; manager.OnGui/OnDraw run in on_draw, inside the
// open GUI frame, exactly where the old loop called them between ImGui
// NewFrame and Render.
//
// PieceTextures::load needs the WGPUDevice, but host on_init runs BEFORE any
// SDL/GPU object exists (task-2 contract: pre-SDL argv + settings hook), so
// the old main's load-at-startup is DEFERRED to the first on_iterate, the
// earliest point where app.device() is valid (Windowed / HeadlessNull).
// HeadlessNone never has a device: Manager starts without art and
// Manager::drawPiece falls back to letters (PieceTextures::texture returns 0
// while unloaded) — the same fallback as a failed load.

#include "Manager.h"
#include "PieceTextures.h"

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"

#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <optional>

namespace {

struct ChessApp : app::AppCallbacks {
  app::ImGuiLayer gui_layer;
  std::optional<PieceTextures> piece_art;  // device-dependent init is lazy
  std::optional<Manager> manager;          // created in on_init (board print)
  bool started = false;
  int smoke_frames = -1;  // --smoke-frames N: exit after N iterates

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "Chess";
    app.settings.clear_color[0] = 0.05f;
    app.settings.clear_color[1] = 0.05f;
    app.settings.clear_color[2] = 0.05f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);
    for (int i = 1; i < argc; ++i)
      if (SDL_strcmp(argv[i], "--smoke-frames") == 0 && i + 1 < argc) smoke_frames = std::atoi(argv[++i]);
    // HeadlessNone never creates a device, so ImGuiLayer::init would fail and
    // the host treats a GUI init failure as fatal. Pure-logic mode skips GUI
    // entirely (on_draw never runs there anyway).
    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) app.attach_gui(gui_layer);

    piece_art.emplace();
    manager.emplace();  // prints the initial board, like the old main
    manager->SetPieceArt(&*piece_art);
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float dt) override {
    if (!started) {
      // Old main order: texture load BEFORE Manager::Start. Deferred to the
      // first iterate because on_init runs pre-device (HeadlessNone: skip).
      if (app.device() && !piece_art->load(app.device())) SDL_Log("Chess: piece textures unavailable, drawing letters instead");
      manager->Start();
      started = true;
      SDL_Log("Chess Started");
    }
    manager->Update(dt);
    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App&, WGPURenderPassEncoder) override {
    manager->OnGui();
    manager->OnDraw();
  }

  void on_shutdown(app::App&) override { SDL_Log("Exiting Chess"); }
};

}  // namespace

MOBAGEN_MAIN(ChessApp)
