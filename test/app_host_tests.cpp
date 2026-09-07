// ============================================================================
// App host unit tests — window-free coverage of the SDL3 callback host.
// ============================================================================
// The host lifecycle drivers (app::host_init/host_event/host_iterate/
// host_quit, app.cpp) are driven DIRECTLY. The SDL_App* trampolines in
// sdl_app.cpp are deliberately never referenced: sdl_app.o synthesizes
// main() from SDL3's main header and would collide with doctest's main
// (test/Test.cpp) once an archive member gets pulled for its symbols.
//
// Draw/iterate off-by-one contract (app.cpp run_frame, asserted below):
// on_iterate runs first, the pending exit request is consumed second, the
// GPU section (which calls on_draw) runs last. A frame that ends the loop —
// whether on_iterate returns non-CONTINUE or calls App::request_exit() —
// therefore NEVER executes its own GPU section: draws == iterations - 1
// when exiting from the iterate callback. Exiting from host_event instead
// stops the loop before another iterate runs: draws == iterations completed.
//
// Input edge contract (input/input_state.hpp + host_iterate): pressed() is
// a went-down-this-frame edge; host_iterate clears the edges at its END
// (events are delivered between iterates), so an edge is visible exactly
// between host_event and the NEXT host_iterate — asserted there.
#include <doctest/doctest.h>

#include "app/app.hpp"
#include "app/app_settings.hpp"

#include <string>
#include <vector>

namespace {

// Counts + records every host callback invocation so tests can assert the
// host's call order (init -> event -> iterate xN [-> draw xN] -> shutdown).
struct RecordingCallbacks : app::AppCallbacks {
  bool fail_init = false;
  bool parse_flags = false;  // run AppSettings::parse(argc, argv, settings) in on_init
  int exit_at_iteration = 0;
  bool exit_via_request_exit = false;  // request_exit() on the exit frame vs returning SUCCESS
  int iterates = 0;
  int draws = 0;
  int shutdowns = 0;
  bool saw_pass = false;
  std::vector<std::string> seq;

  SDL_AppResult on_init(app::App& a, int argc, char** argv) override {
    seq.emplace_back("init");
    if (parse_flags) (void)app::AppSettings::parse(argc, argv, a.settings);
    if (fail_init) return SDL_APP_FAILURE;
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_event(app::App& a, const SDL_Event& e) override {
    (void)a;
    (void)e;
    seq.emplace_back("event");
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& a, float dt) override {
    (void)dt;
    seq.emplace_back("iterate");
    ++iterates;
    if (exit_at_iteration != 0 && iterates >= exit_at_iteration) {
      if (exit_via_request_exit) {
        a.request_exit();  // consumed later the SAME frame, before the GPU section
        return SDL_APP_CONTINUE;
      }
      return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App& a, WGPURenderPassEncoder pass) override {
    (void)a;
    seq.emplace_back("draw");
    ++draws;
    if (pass != nullptr) saw_pass = true;
  }

  void on_shutdown(app::App& a) override {
    (void)a;
    ++shutdowns;
    seq.emplace_back("shutdown");
  }
};

// SDL_Init(0) — all the headless host modes run — does NOT start the event
// queue (SDL3 only initializes it under SDL_INIT_EVENTS), and SDL_PushEvent
// on an inactive queue is a graceful no-op returning false. Tests that need
// the real push/poll path start the EVENTS subsystem themselves — never
// VIDEO, so no window is ever created.
void start_event_subsystem() {
  if (!SDL_WasInit(SDL_INIT_EVENTS)) REQUIRE(SDL_InitSubSystem(SDL_INIT_EVENTS));
  SDL_PumpEvents();
  SDL_Event ignored;
  while (SDL_PollEvent(&ignored)) {
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// (a) AppSettings defaults + --mobagen-* flag parsing
// ---------------------------------------------------------------------------
TEST_CASE("app host: AppSettings defaults") {
  const app::AppSettings s;
  CHECK(s.render_mode == app::AppSettings::RenderMode::Windowed);
  CHECK(s.width == 1280);
  CHECK(s.height == 800);
  CHECK(SDL_strcmp(s.title, "MoBaGEn App") == 0);
  CHECK(s.resizable);
  CHECK(!s.high_pixel_density);
  CHECK(s.power_preference == WGPUPowerPreference_HighPerformance);
  CHECK(s.clear_color[3] == 1.0f);
}

TEST_CASE("app host: AppSettings parse render-mode flags") {
  char arg0[] = "CoreTests";
  char headless_flag[] = "--mobagen-headless";
  char null_gpu_flag[] = "--mobagen-null-gpu";

  SUBCASE("empty argv leaves defaults and reports nothing recognized") {
    app::AppSettings s;
    char* argv[] = {arg0, nullptr};
    CHECK(!app::AppSettings::parse(1, argv, s));
    CHECK(s.render_mode == app::AppSettings::RenderMode::Windowed);
  }
  SUBCASE("--mobagen-headless maps to HeadlessNone") {
    app::AppSettings s;
    char* argv[] = {arg0, headless_flag, nullptr};
    CHECK(app::AppSettings::parse(2, argv, s));
    CHECK(s.render_mode == app::AppSettings::RenderMode::HeadlessNone);
  }
  SUBCASE("--mobagen-null-gpu maps to HeadlessNull") {
    app::AppSettings s;
    char* argv[] = {arg0, null_gpu_flag, nullptr};
    CHECK(app::AppSettings::parse(2, argv, s));
    CHECK(s.render_mode == app::AppSettings::RenderMode::HeadlessNull);
  }
  SUBCASE("unknown arguments are ignored") {
    app::AppSettings s;
    char opt[] = "--board";
    char val[] = "fixture";
    char* argv[] = {arg0, opt, val, nullptr};
    CHECK(!app::AppSettings::parse(3, argv, s));
    CHECK(s.render_mode == app::AppSettings::RenderMode::Windowed);
  }
  SUBCASE("last flag wins") {
    app::AppSettings headless_then_null;
    char* argv1[] = {arg0, headless_flag, null_gpu_flag, nullptr};
    CHECK(app::AppSettings::parse(3, argv1, headless_then_null));
    CHECK(headless_then_null.render_mode == app::AppSettings::RenderMode::HeadlessNull);

    app::AppSettings null_then_headless;
    char* argv2[] = {arg0, null_gpu_flag, headless_flag, nullptr};
    CHECK(app::AppSettings::parse(3, argv2, null_then_headless));
    CHECK(null_then_headless.render_mode == app::AppSettings::RenderMode::HeadlessNone);
  }
}

// ---------------------------------------------------------------------------
// (b) WebGPUContext null-backend lifecycle (native only; emdawnwebgpu has no
//     null backend). Synchronous WaitAny requests — no sleeps anywhere.
// ---------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__
TEST_CASE("app host: WebGPUContext null device lifecycle") {
  app::ContextDesc desc;
  desc.backend_type = WGPUBackendType_Null;
  desc.want_surface = false;  // surfaceless: works on display-less CI boxes

  app::WebGPUContext ctx;
  REQUIRE(ctx.init(desc));
  CHECK(ctx.device() != nullptr);
  CHECK(ctx.queue() != nullptr);
  CHECK(ctx.surface() == nullptr);

  ctx.shutdown();
  CHECK(ctx.device() == nullptr);
  ctx.shutdown();  // idempotent: a second call must be a harmless no-op

  {  // destructor-only path: a context never explicitly shut down
    app::WebGPUContext raii_ctx;
    CHECK(raii_ctx.init(desc));
    CHECK(raii_ctx.device() != nullptr);
  }
}
#endif  // __EMSCRIPTEN__

// ---------------------------------------------------------------------------
// (c) Full host lifecycle in HeadlessNone: window-free, GPU-free.
// ---------------------------------------------------------------------------
TEST_CASE("app host: HeadlessNone lifecycle order and input feed") {
  RecordingCallbacks cb;
  cb.parse_flags = true;
  cb.exit_at_iteration = 3;

  app::App app;
  app.callbacks = &cb;

  char arg0[] = "CoreTests";
  char headless_flag[] = "--mobagen-headless";
  char* argv[] = {arg0, headless_flag, nullptr};

  REQUIRE(app::host_init(app, 2, argv) == SDL_APP_CONTINUE);
  CHECK(app.settings.render_mode == app::AppSettings::RenderMode::HeadlessNone);
  CHECK(app.window == nullptr);   // headless: no window was ever created
  CHECK(app.device() == nullptr); // HeadlessNone: zero GPU objects
  CHECK(cb.seq == std::vector<std::string>{"init"});

  // Synthetic key-down through the real SDL event queue, then through the
  // host — the same path a windowed SDL_PollEvent loop would take.
  start_event_subsystem();
  SDL_Event ev{};
  ev.type = SDL_EVENT_KEY_DOWN;
  ev.key.key = SDLK_F9;
  REQUIRE(SDL_PushEvent(&ev));
  SDL_Event polled{};
  bool got_key = false;
  for (int i = 0; i < 16 && !got_key; ++i) {
    got_key = SDL_PollEvent(&polled) && polled.type == SDL_EVENT_KEY_DOWN && polled.key.key == SDLK_F9;
  }
  REQUIRE(got_key);
  CHECK(app::host_event(app, polled) == SDL_APP_CONTINUE);

  // Input contract: pressed() is a this-frame edge; host_iterate clears the
  // edges at its END, so the edge is visible exactly HERE — after host_event,
  // before the next host_iterate.
  CHECK(app.input.pressed(SDLK_F9));
  CHECK(app.input.held(SDLK_F9));

  CHECK(app::host_iterate(app) == SDL_APP_CONTINUE);
  CHECK(!app.input.pressed(SDLK_F9));  // edge consumed by the iterate
  CHECK(app.input.held(SDLK_F9));      // held state persists

  CHECK(app::host_iterate(app) == SDL_APP_CONTINUE);
  // 3rd on_iterate returns SDL_APP_SUCCESS (exit_at_iteration == 3).
  CHECK(app::host_iterate(app) == SDL_APP_SUCCESS);

  app::host_quit(app);
  CHECK(cb.iterates == 3);
  CHECK(cb.draws == 0);  // HeadlessNone never calls on_draw
  CHECK(cb.shutdowns == 1);
  CHECK(cb.seq == std::vector<std::string>({"init", "event", "iterate", "iterate", "iterate", "shutdown"}));
}

// ---------------------------------------------------------------------------
// (d) Failure path: on_init FAILURE short-circuits the whole loop; SDL (and
//     the mirrored host_quit below) still runs the quit path cleanly.
// ---------------------------------------------------------------------------
TEST_CASE("app host: init failure short-circuits to shutdown") {
  RecordingCallbacks cb;
  cb.fail_init = true;

  app::App app;
  app.callbacks = &cb;

  char arg0[] = "CoreTests";
  char* argv[] = {arg0, nullptr};

  CHECK(app::host_init(app, 1, argv) == SDL_APP_FAILURE);
  CHECK(app.window == nullptr);
  CHECK(app.device() == nullptr);

  // SDL never iterates after a non-CONTINUE SDL_AppInit; only SDL_AppQuit
  // runs — host_quit must tolerate the never-initialized SDL/GPU state.
  app::host_quit(app);
  CHECK(cb.iterates == 0);
  CHECK(cb.draws == 0);
  CHECK(cb.shutdowns == 1);
  CHECK(cb.seq == std::vector<std::string>({"init", "shutdown"}));
}

// ---------------------------------------------------------------------------
// (e) Full host integration in HeadlessNull: Dawn null device + offscreen
//     frame targets (native only — web falls back to HeadlessNone).
// ---------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__
TEST_CASE("app host: HeadlessNull integration renders offscreen") {
  RecordingCallbacks cb;
  cb.parse_flags = true;
  cb.exit_at_iteration = 3;
  cb.exit_via_request_exit = true;

  app::App app;
  app.callbacks = &cb;
  app.settings.width = 64;  // small offscreen target: the null device
  app.settings.height = 64; // validates frames but never touches a GPU

  char arg0[] = "CoreTests";
  char null_gpu_flag[] = "--mobagen-null-gpu";
  char* argv[] = {arg0, null_gpu_flag, nullptr};

  REQUIRE(app::host_init(app, 2, argv) == SDL_APP_CONTINUE);
  CHECK(app.settings.render_mode == app::AppSettings::RenderMode::HeadlessNull);  // no fallback occurred
  CHECK(app.device() != nullptr);
  CHECK(app.window == nullptr);

  // Off-by-one contract (file header): the 3rd frame calls request_exit()
  // inside on_iterate; run_frame consumes the pending exit BEFORE the GPU
  // section, so that frame never draws -> draws == iterations - 1 == 2.
  CHECK(app::host_iterate(app) == SDL_APP_CONTINUE);
  CHECK(app::host_iterate(app) == SDL_APP_CONTINUE);
  CHECK(app::host_iterate(app) == SDL_APP_SUCCESS);

  CHECK(cb.iterates == 3);
  CHECK(cb.draws == 2);  // NOT 3: the exiting frame skips its GPU section
  CHECK(cb.saw_pass);    // every on_draw received a valid render pass

  app::host_quit(app);
  CHECK(cb.shutdowns == 1);
  CHECK(cb.seq == std::vector<std::string>({"init", "iterate", "draw", "iterate", "draw", "iterate", "shutdown"}));
}
#endif  // __EMSCRIPTEN__
