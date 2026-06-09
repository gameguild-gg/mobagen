#include <SDL3/SDL_main.h>
#include "engine/Engine.h"
#include "engine/EngineSettings.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

// ---------------------------------------------------------------------------
// Embedded RML document
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

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int, char**) {
  EngineSettings settings;
  settings.useImGui = false;
  settings.useRmlUi = true;
  settings.vsync    = true;

  // Match the document background color (#1a1a2e) so there's no flash
  // if the WebGPU clear happens before RmlUi fills the viewport.
  settings.clearColor[0] = 0.102f;
  settings.clearColor[1] = 0.102f;
  settings.clearColor[2] = 0.180f;
  settings.clearColor[3] = 1.0f;

  auto engine = new Engine(settings);

  if (!engine->Start("RmlUi + SDL3 + WebGPU Demo")) {
    return 1;
  }

  auto* ctx = engine->window->rmlContext;

  if (!ctx) {
    SDL_Log("RmlUi not initialized.");
    return 1;
  }

  // Load document directly from embedded string
  Rml::ElementDocument* doc =
      ctx->LoadDocumentFromMemory(kDemoRml, "demo.rml");

  if (!doc) {
    SDL_Log("Failed to load RML document.");
    return 1;
  }
  doc->Show();

  // Diagnostic overlay showing actual window/pixel dimensions at runtime.
  Rml::Element* diag = doc->GetElementById("diag");
  engine->onTick = [window = engine->window, diag]() {
    if (!diag) return;
    int logW = 0, logH = 0;
    SDL_GetWindowSize(window->sdlWindow, &logW, &logH);
    int pxW = 0, pxH = 0;
    SDL_GetWindowSizeInPixels(window->sdlWindow, &pxW, &pxH);
    float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window->sdlWindow));
    char buf[256];
    SDL_snprintf(buf, sizeof(buf),
        "SDL_WindowSize: %dx%d\n"
        "SDL_WindowSizeInPixels: %dx%d\n"
        "DisplayContentScale: %.2f\n"
        "ComputedPhys(log*scale): %.0fx%.0f",
        logW, logH,
        pxW, pxH,
        scale,
        logW * scale, logH * scale);
    diag->SetInnerRML(buf);
  };

  SDL_Log("RmlUi demo started. F8 = debugger, ESC = exit.");

  engine->Run();

  doc->Close();
  engine->Exit();
  delete engine;
  return 0;
}
