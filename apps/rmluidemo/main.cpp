// rmluidemo — core app host migration (core-app-host plan, todo 16).
// The RmlUi document renders through the host GUI layer (app::RmlUiLayer,
// core_rmlui): the layer owns the ImGui WebGPU pipeline plus the RmlUi boot,
// fonts and debugger; this app keeps only its embedded RML document, the
// document load, the #diag diagnostics element and F8/ESC handling.

#include "app/sdl_app.hpp"
#include "rmlui/rmlui_layer.hpp"

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

// ---------------------------------------------------------------------------
// Embedded RML document (unchanged)
// ---------------------------------------------------------------------------
static const char kDemoRml[] = R"(
<rml>
<head>
  <title>RmlUi + SDL3 + WebGPU Demo</title>
  <style>
    /* Flex body: centers #window both axes without needing transform hacks.
       #fullbg and #diag are position:absolute so they leave the flex flow. */
    body {
      width: 100%;
      height: 100%;
      font-family: AppFont;
      font-size: 1.5vw;
      color: #e0e0e0;
    }
    #fullbg {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: #1a1a2e;
      z-index: -1;
    }
    /* Absolute full-screen flex layer: top/right/bottom/left:0 stretches to
       the containing block without relying on height:100% percentage resolution. */
    #center-layer {
      position: absolute;
      top: 0;
      right: 0;
      bottom: 0;
      left: 0;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    /* Box: 60% wide x 70% tall, auto-height content, centered by parent flex. */
    #window {
      display: block;
      width: 60%;
      background: #1e2a4a;
      padding: 2em 2.5em;
      text-align: center;
    }
    h1 {
      display: block;
      color: #e94560;
      font-size: 2em;
      margin-bottom: 0.5em;
    }
    p {
      display: block;
      margin: 0.4em 0;
      font-size: 1em;
      line-height: 1.4;
    }
    strong { color: #e94560; }
    em { color: #8a8aff; font-style: italic; }
    hr {
      display: block;
      border: 0;
      height: 1px;
      background: #0f3460;
      margin: 1em 0;
    }
    .feature-box {
      display: block;
      background: #16213e;
      padding: 1em;
      margin: 0.8em 0;
      text-align: left;
    }
    .feature-box p {
      font-size: 0.9em;
      color: #aab;
    }
    .check { color: #4ecca3; }
    .info {
      font-size: 0.8em;
      color: #888;
      margin-top: 0.8em;
    }
    kbd {
      background: #0f3460;
      color: #e0e0ff;
      padding: 1px 6px;
      font-size: 0.9em;
    }
    #diag {
      position: absolute;
      top: 0;
      left: 0;
      font-size: 0.8vw;
      color: #ff6;
      font-family: monospace;
      background: rgba(0,0,0,0.6);
      padding: 4px 8px;
      white-space: pre;
      z-index: 100;
    }
  </style>
</head>
<body>
  <div id="fullbg"></div>
  <div id="center-layer">
    <div id="window">
      <h1>RmlUi + WebGPU</h1>
      <p>A retained-mode <strong>HTML/CSS</strong> UI rendered with a custom <em>WebGPU</em> backend</p>
      <hr />
      <div class="feature-box">
        <p><span class="check">&#x2022;</span> RmlUi core library (v6.2)</p>
        <p><span class="check">&#x2022;</span> SDL3 platform backend</p>
        <p><span class="check">&#x2022;</span> Custom WebGPU render backend</p>
        <p><span class="check">&#x2022;</span> Dawn native WebGPU implementation</p>
        <p><span class="check">&#x2022;</span> Zero ImGui usage in this demo</p>
      </div>
      <p class="info">Press <kbd>F8</kbd> to toggle the RmlUi debugger</p>
      <p class="info">Press <kbd>ESC</kbd> or close the window to exit</p>
    </div>
  </div>
  <p id="diag">diag: waiting...</p>
</body>
</rml>
)";

struct RmlApp : app::AppCallbacks {
  app::RmlUiLayer* rml = nullptr;  // static layer from on_init (never attached in HeadlessNone)
  Rml::Element* diag = nullptr;    // #diag element of the loaded document
  bool doc_loaded = false;         // lazy load: the context exists only after layer init
  int smoke_frames = 0;            // --smoke-frames N: exit after N iterates (0 = off)

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    app.settings.title = "RmlUi + SDL3 + WebGPU Demo";
    app.settings.width = 1280;
    app.settings.height = 720;
    app.settings.high_pixel_density = true;  // old window flag
    // Old frame clear color #1a1a2e (old main.cpp:682).
    app.settings.clear_color[0] = 0.102f;
    app.settings.clear_color[1] = 0.102f;
    app.settings.clear_color[2] = 0.180f;
    app.settings.clear_color[3] = 1.00f;

    app::AppSettings::parse(argc, argv, app.settings);
    for (int i = 1; i < argc - 1; ++i)
      if (SDL_strcmp(argv[i], "--smoke-frames") == 0) smoke_frames = SDL_atoi(argv[i + 1]);

    // The layer composes ImGuiLayer internally (RmlUi rides ImGui's WebGPU
    // pipeline) — attach ONLY this layer, and never in HeadlessNone, where
    // its ImGui base has no device and init failure would kill the run.
    static app::RmlUiLayer rml_layer;
    rml = &rml_layer;
    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone)
      app.attach_gui(rml_layer);
    return SDL_APP_CONTINUE;
  }

  // Old loop key handling (old main.cpp:604-607). The layer's process_event
  // (host-called) already fed the event to RmlSDL/ImGui before this runs.
  SDL_AppResult on_event(app::App& app, const SDL_Event& e) override {
    if (e.type == SDL_EVENT_KEY_DOWN) {
      if (e.key.key == SDLK_ESCAPE) {  // old: ESC stops the loop -> exit 0
        app.request_exit();
        return SDL_APP_SUCCESS;
      }
      if (e.key.key == SDLK_F8 && rml != nullptr && rml->context() != nullptr)
        Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
    }
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float /*dt*/) override {
    // Lazy document load: the layer initializes during host_init AFTER
    // on_init, so its RmlUi context first exists at the first iterate.
    if (!doc_loaded && rml != nullptr && rml->context() != nullptr) {
      Rml::ElementDocument* doc = rml->context()->LoadDocumentFromMemory(kDemoRml, "demo.rml");
      if (doc != nullptr) {
        doc->Show();
        diag = doc->GetElementById("diag");
        SDL_Log("RmlUi demo started. F8 = debugger, ESC = exit.");
      } else {
        SDL_Log("Failed to load RML document.");
      }
      doc_loaded = true;
    }

    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();
    return SDL_APP_CONTINUE;
  }

  // Per-frame diagnostics overlay (old main.cpp:638-652, kept verbatim):
  // window geometry written into the #diag RML element. The layer already ran
  // the context update for this frame, so the text shows up one frame later —
  // imperceptible for geometry values.
  void on_draw(app::App& app, WGPURenderPassEncoder /*pass*/) override {
    if (diag == nullptr || app.window == nullptr) return;
    int logW = 0, logH = 0;
    SDL_GetWindowSize(app.window, &logW, &logH);
    int pxW = 0, pxH = 0;
    SDL_GetWindowSizeInPixels(app.window, &pxW, &pxH);
    const float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(app.window));
    char buf[256];
    SDL_snprintf(buf, sizeof(buf),
                 "SDL_WindowSize: %dx%d\n"
                 "SDL_WindowSizeInPixels: %dx%d\n"
                 "DisplayContentScale: %.2f\n"
                 "ComputedPhys(log*scale): %.0fx%.0f",
                 logW, logH, pxW, pxH, scale, logW * scale, logH * scale);
    diag->SetInnerRML(buf);
  }
};

MOBAGEN_MAIN(RmlApp)
