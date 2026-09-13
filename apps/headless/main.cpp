// Fixed-step ECS sim demo on the core app host, headless-only (todo 18).
//
// The host (app::App + the SDL3 callback trampolines behind MOBAGEN_MAIN) owns
// the ecs::World and the jobs::Scheduler; this file rehosts the old hand-rolled
// main() onto the lifecycle hooks. Render mode: native defaults to HeadlessNull
// (Dawn null device, offscreen; on_draw never draws anything here), web
// defaults to HeadlessNone (null backend unavailable under emdawnwebgpu);
// --mobagen-headless / --mobagen-null-gpu still win over the default
// (AppSettings::parse reports whether a flag was seen).
//
// stdout is compared byte-for-byte (docs/CI), and the mandated captures merge
// stderr (2>&1), so on_init silences the host's SDL_Log chatter (context /
// device-ready / device-lost lines all go to stderr) — the old binary was
// silent there.
#include "app/sdl_app.hpp"
#include "scene/transform.hpp"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <iostream>

namespace {

// Simulation state component (replaces the OOP HeadlessTestObject fields).
struct SimState {
  float totalTime = 0.0f;
  float maxRunTime = 5.0f;  // Run for 5 seconds
  int frameCount = 0;
};

struct HeadlessApp : app::AppCallbacks {
  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    std::printf("Creating Headless World\n");  // old main()'s first statement

    SDL_SetLogOutputFunction(
        [](void*, int, SDL_LogPriority, const char*) {}, nullptr);

    const bool flagged = app::AppSettings::parse(argc, argv, app.settings);
    if (!flagged) {
#ifdef __EMSCRIPTEN__
      app.settings.render_mode = app::AppSettings::RenderMode::HeadlessNone;
#else
      app.settings.render_mode = app::AppSettings::RenderMode::HeadlessNull;
#endif
    }

    // Simulation entity with state + a root Transform, in the host-owned world.
    ecs::Entity simEnt = app.world.create();
    app.world.add<SimState>(simEnt);
    app.world.add<scene::Transform>(simEnt);

    std::printf("Headless World Created\n");
    std::printf("Starting Headless Simulation\n");
    return SDL_APP_CONTINUE;
  }

  // Old main() ran the whole fixed-step loop to completion in one go (tight
  // while, no sleep on native); ported faithfully: the first on_iterate steps
  // the sim to completion — identical 60Hz step sequence, identical floats,
  // identical output bytes — then requests exit (consumed this frame, before
  // the GPU section, so exit code is 0). Host dt is ignored: the sim advances
  // in fixed steps, never wall-clock ones.
  SDL_AppResult on_iterate(app::App& app, float dt) override {
    (void)dt;
    constexpr float kTargetDt = 1.0f / 60.0f;  // 60 FPS simulation step
    bool running = true;

    while (running) {
      // Update system: advance time, log, check stop condition.
      app.world.view<SimState>([&](ecs::Entity, SimState& s) {
        s.totalTime += kTargetDt;
        s.frameCount++;

        // Log progress every second (every 60 frames at 60 FPS).
        if (s.frameCount % 60 == 0) {
          std::printf("Headless simulation running: %.2f seconds, frame %d\n", s.totalTime, s.frameCount);
        }

        // Exit after maxRunTime seconds.
        if (s.totalTime >= s.maxRunTime) {
          std::printf("Headless simulation completed after %.2f seconds (%d frames)\n", s.totalTime, s.frameCount);
          running = false;
        }
      });
    }

    app.request_exit();
    return SDL_APP_CONTINUE;
  }

  // Old order: sched.shutdown() -> "Stopped" -> cout line. The host owns the
  // scheduler and shuts it down AFTER on_shutdown (silently), so printing here
  // reproduces the old stdout byte order exactly.
  void on_shutdown(app::App&) override {
    std::printf("Headless Simulation Stopped\n");
    std::cout << "Headless simulation completed successfully!" << std::endl;
  }
};

}  // namespace

MOBAGEN_MAIN(HeadlessApp)
