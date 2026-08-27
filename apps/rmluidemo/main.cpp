// rmluidemo — DOD port.
// Renders an RmlUi document through ImGui's Dawn/WebGPU render pipeline.
// Bootstrap: ecs::World + jobs::Scheduler (DOD) replaces the deleted OOP Engine.

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#if defined(SDL_PLATFORM_APPLE) && !defined(__EMSCRIPTEN__)
#  include <SDL3/SDL_metal.h>
#endif

#include <webgpu/webgpu.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Debugger/FontSource.h>  // courier_prime_code[]
#include <RmlUi_Platform_SDL.h>         // SystemInterface_SDL, RmlSDL::InputEventHandler

// DOD bootstrap
#include "ecs/world.hpp"
#include "input/input_state.hpp"
#include "jobs/scheduler.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

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

// ---------------------------------------------------------------------------
// WebGPU async request helpers (adapter / device)
// ---------------------------------------------------------------------------
struct AdapterReq {
  WGPUAdapter adapter = nullptr;
  bool done = false;
};
struct DeviceReq {
  WGPUDevice device = nullptr;
  bool done = false;
};

static void onAdapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg, void* ud1, void*) {
  auto* r = static_cast<AdapterReq*>(ud1);
  if (st == WGPURequestAdapterStatus_Success)
    r->adapter = a;
  else
    SDL_Log("RequestAdapter failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
  r->done = true;
}

static void onDevice(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg, void* ud1, void*) {
  auto* r = static_cast<DeviceReq*>(ud1);
  if (st == WGPURequestDeviceStatus_Success)
    r->device = d;
  else
    SDL_Log("RequestDevice failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
  r->done = true;
}

static void onUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
  SDL_Log("[WGPU error type=%d]: %.*s", (int)type, (int)msg.length, msg.data ? msg.data : "");
}

static void onDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView msg, void*, void*) {
  SDL_Log("[WGPU device lost reason=%d]: %.*s", (int)reason, (int)msg.length, msg.data ? msg.data : "");
}

static bool pumpUntil(WGPUInstance inst, bool& flag, const char* op, Uint64 timeoutMs = 10000) {
  const Uint64 start = SDL_GetTicks();
  while (!flag) {
    wgpuInstanceProcessEvents(inst);
    if (SDL_GetTicks() - start > timeoutMs) {
      SDL_Log("%s timed out after %llu ms", op, static_cast<unsigned long long>(timeoutMs));
      return false;
    }
    SDL_Delay(1);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Platform-specific WebGPU surface creation from an SDL3 window
// ---------------------------------------------------------------------------
#if defined(SDL_PLATFORM_APPLE) && !defined(__EMSCRIPTEN__)
static SDL_MetalView g_metal_view = nullptr;
#endif

static WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window) {
  WGPUSurfaceDescriptor desc = {};
#if defined(__EMSCRIPTEN__)
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
  canvasDesc.selector = {"#canvas", WGPU_STRLEN};
  desc.nextInChain = &canvasDesc.chain;
#elif defined(SDL_PLATFORM_WIN32)
  WGPUSurfaceSourceWindowsHWND hwndDesc = {};
  hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
  hwndDesc.hinstance = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  hwndDesc.hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  desc.nextInChain = &hwndDesc.chain;
#elif defined(SDL_PLATFORM_APPLE)
  g_metal_view = SDL_Metal_CreateView(window);
  WGPUSurfaceSourceMetalLayer metalDesc = {};
  metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
  metalDesc.layer = SDL_Metal_GetLayer(g_metal_view);
  desc.nextInChain = &metalDesc.chain;
#elif defined(SDL_PLATFORM_LINUX)
  void* xdisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  uint64_t xwindow = static_cast<uint64_t>(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
  WGPUSurfaceSourceXlibWindow xlibDesc = {};
  xlibDesc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
  xlibDesc.display = xdisplay;
  xlibDesc.window = xwindow;
  desc.nextInChain = &xlibDesc.chain;
#else
#  error "Unsupported platform for WebGPU surface creation"
#endif
  WGPUSurface surface = wgpuInstanceCreateSurface(instance, &desc);
  if (!surface) SDL_Log("wgpuInstanceCreateSurface failed");
  return surface;
}

// ---------------------------------------------------------------------------
// RmlImGuiRenderer — Rml::RenderInterface via ImGui background draw list.
//
// RmlUi layout geometry (CSS backgrounds, borders, text glyphs) is routed
// through ImGui's draw list API so that imgui_impl_wgpu submits it via the
// same WebGPU render pass as the rest of the frame.  No separate render
// pipeline is needed — ImGui is the GPU host.
//
// Untextured geometry (solid CSS colours): uses ImGui's font-atlas white
//   pixel UV so vertex colours pass through unmodified.
// Textured geometry (font glyphs): WGPUTextureView cast as ImTextureID;
//   imgui_impl_wgpu creates the required bind group automatically.
// ---------------------------------------------------------------------------
class RmlImGuiRenderer : public Rml::RenderInterface {
public:
  void set_device(WGPUDevice dev, WGPUQueue q) {
    device_ = dev;
    queue_ = q;
  }

  // ----- geometry --------------------------------------------------------
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
    const auto handle = static_cast<Rml::CompiledGeometryHandle>(++next_id_);
    Geometry& geo = geometries_[handle];
    geo.vertices.assign(vertices.begin(), vertices.end());
    geo.indices.assign(indices.begin(), indices.end());
    return handle;
  }

  void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { geometries_.erase(handle); }

  void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
    auto it = geometries_.find(handle);
    if (it == geometries_.end()) return;
    const Geometry& geo = it->second;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Apply per-call clip rect if the scissor is active.
    if (scissor_enabled_) {
      dl->PushClipRect(ImVec2(static_cast<float>(scissor_.Left()), static_cast<float>(scissor_.Top())),
                       ImVec2(static_cast<float>(scissor_.Right()), static_cast<float>(scissor_.Bottom())), true);
    }

    // Untextured (CSS solid colours): keep the draw list's implicit default
    // texture (the ImGui font atlas) and use its white-pixel UV so vertex
    // colour passes through unmodified.  Textured (font glyphs): push the
    // WGPUTextureView stored as TextureHandle; imgui_impl_wgpu creates the
    // required bind group automatically.
    const bool use_white = (texture == 0);
    const ImVec2 white_uv = ImGui::GetIO().Fonts->TexUvWhitePixel;

    if (!use_white) dl->PushTextureID(static_cast<ImTextureID>(texture));

    const int vtx_count = static_cast<int>(geo.vertices.size());
    const int idx_count = static_cast<int>(geo.indices.size());
    const ImDrawIdx vtx_base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    dl->PrimReserve(idx_count, vtx_count);

    for (const Rml::Vertex& v : geo.vertices) {
      dl->_VtxWritePtr->pos = ImVec2(v.position.x + translation.x, v.position.y + translation.y);
      dl->_VtxWritePtr->uv = use_white ? white_uv : ImVec2(v.tex_coord.x, v.tex_coord.y);
      dl->_VtxWritePtr->col = IM_COL32(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
      ++dl->_VtxWritePtr;
    }
    dl->_VtxCurrentIdx += static_cast<unsigned int>(vtx_count);

    for (int idx : geo.indices) {
      *dl->_IdxWritePtr++ = static_cast<ImDrawIdx>(vtx_base + idx);
    }

    if (!use_white) dl->PopTextureID();
    if (scissor_enabled_) dl->PopClipRect();
  }

  // ----- textures --------------------------------------------------------
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override {
    if (!device_ || !queue_) return {};

    const uint32_t w = static_cast<uint32_t>(source_dimensions.x);
    const uint32_t h = static_cast<uint32_t>(source_dimensions.y);
    const uint32_t tight_bpr = w * 4u;
    // WebGPU requires bytesPerRow to be a multiple of 256.
    const uint32_t aligned_bpr = (tight_bpr + 255u) & ~255u;

    std::vector<Rml::byte> padded;
    const Rml::byte* upload_data = source.data();
    if (aligned_bpr != tight_bpr) {
      padded.resize(static_cast<size_t>(aligned_bpr) * h, 0);
      for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(padded.data() + row * aligned_bpr, source.data() + row * tight_bpr, tight_bpr);
      }
      upload_data = padded.data();
    }

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.label = {"rmlui_font_tex", WGPU_STRLEN};
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size.width = w;
    texDesc.size.height = h;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture tex = wgpuDeviceCreateTexture(device_, &texDesc);
    if (!tex) return {};

    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = tex;
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = aligned_bpr;
    layout.rowsPerImage = h;

    WGPUExtent3D extent = WGPU_EXTENT_3D_INIT;
    extent.width = w;
    extent.height = h;
    extent.depthOrArrayLayers = 1;

    const size_t upload_size = static_cast<size_t>(aligned_bpr) * h;
    wgpuQueueWriteTexture(queue_, &dst, upload_data, upload_size, &layout, &extent);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(tex, &viewDesc);
    if (!view) {
      wgpuTextureDestroy(tex);
      wgpuTextureRelease(tex);
      return {};
    }

    // TextureHandle stores the WGPUTextureView pointer as a uintptr_t.
    // imgui_impl_wgpu casts ImTextureID back to WGPUTextureView and
    // creates the required bind group automatically.
    const Rml::TextureHandle handle = static_cast<Rml::TextureHandle>(reinterpret_cast<uintptr_t>(view));
    textures_[handle] = tex;
    return handle;
  }

  Rml::TextureHandle LoadTexture(Rml::Vector2i& /*dims*/, const Rml::String& /*src*/) override {
    return {};  // file-based textures not needed for this CSS-only demo
  }

  void ReleaseTexture(Rml::TextureHandle handle) override {
    auto it = textures_.find(handle);
    if (it == textures_.end()) return;
    WGPUTexture tex = it->second;
    auto* view = reinterpret_cast<WGPUTextureView>(static_cast<uintptr_t>(handle));
    if (view) wgpuTextureViewRelease(view);
    if (tex) {
      wgpuTextureDestroy(tex);
      wgpuTextureRelease(tex);
    }
    textures_.erase(it);
  }

  // ----- scissor ---------------------------------------------------------
  void EnableScissorRegion(bool enable) override { scissor_enabled_ = enable; }

  void SetScissorRegion(Rml::Rectanglei region) override { scissor_ = region; }

private:
  struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
  };

  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  int next_id_ = 0;
  std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometries_;
  std::unordered_map<Rml::TextureHandle, WGPUTexture> textures_;
  bool scissor_enabled_ = false;
  Rml::Rectanglei scissor_{};
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int, char**) {
  // ---- DOD bootstrap ----------------------------------------------------
  ecs::World world;
  jobs::Scheduler scheduler;
  input::InputState input_state;

  // ---- SDL3 init --------------------------------------------------------
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("RmlUi + SDL3 + WebGPU Demo", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // ---- WebGPU: instance -------------------------------------------------
  WGPUInstanceDescriptor instDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
  WGPUInstance instance = wgpuCreateInstance(&instDesc);
  if (!instance) {
    SDL_Log("wgpuCreateInstance failed");
    return 1;
  }

  // ---- WebGPU: surface --------------------------------------------------
  WGPUSurface surface = createSurface(instance, window);
  if (!surface) return 1;

  // ---- WebGPU: adapter (async) ------------------------------------------
  AdapterReq aReq;
  WGPURequestAdapterOptions aOpts = {};
  aOpts.compatibleSurface = surface;
  aOpts.powerPreference = WGPUPowerPreference_HighPerformance;
  WGPURequestAdapterCallbackInfo aCb = {};
  aCb.mode = WGPUCallbackMode_AllowProcessEvents;
  aCb.callback = onAdapter;
  aCb.userdata1 = &aReq;
  wgpuInstanceRequestAdapter(instance, &aOpts, aCb);
  if (!pumpUntil(instance, aReq.done, "requestAdapter") || !aReq.adapter) {
    SDL_Log("No WebGPU adapter available");
    return 1;
  }

  // ---- WebGPU: device (async) -------------------------------------------
  DeviceReq dReq;
  WGPUDeviceDescriptor dDesc = {};
  dDesc.label = {"rmluidemo_device", WGPU_STRLEN};
  dDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
  dDesc.deviceLostCallbackInfo.callback = onDeviceLost;
  WGPURequestDeviceCallbackInfo dCb = {};
  dCb.mode = WGPUCallbackMode_AllowProcessEvents;
  dCb.callback = onDevice;
  dCb.userdata1 = &dReq;
  wgpuAdapterRequestDevice(aReq.adapter, &dDesc, dCb);
  if (!pumpUntil(instance, dReq.done, "requestDevice") || !dReq.device) {
    SDL_Log("WebGPU device request failed");
    return 1;
  }

  WGPUDevice device = dReq.device;
  WGPUQueue queue = wgpuDeviceGetQueue(device);

  // ---- WebGPU: surface format + initial config --------------------------
  WGPUSurfaceCapabilities caps = {};
  wgpuSurfaceGetCapabilities(surface, aReq.adapter, &caps);
  const WGPUTextureFormat surfaceFormat = (caps.formatCount > 0 && caps.formats) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
  wgpuSurfaceCapabilitiesFreeMembers(caps);

  int pxW = 0, pxH = 0;
  SDL_GetWindowSizeInPixels(window, &pxW, &pxH);

  auto configureSurface = [&](int w, int h) {
    if (w <= 0 || h <= 0) return;
    WGPUSurfaceConfiguration cfg = {};
    cfg.device = device;
    cfg.format = surfaceFormat;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
    cfg.width = static_cast<uint32_t>(w);
    cfg.height = static_cast<uint32_t>(h);
    cfg.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface, &cfg);
  };
  configureSurface(pxW, pxH);

  // ---- ImGui ------------------------------------------------------------
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOther(window);

  ImGui_ImplWGPU_InitInfo wgpuInit = {};
  wgpuInit.Device = device;
  wgpuInit.NumFramesInFlight = 3;
  wgpuInit.RenderTargetFormat = surfaceFormat;
  wgpuInit.DepthStencilFormat = WGPUTextureFormat_Undefined;
  ImGui_ImplWGPU_Init(&wgpuInit);

  // ---- RmlUi ------------------------------------------------------------
  SystemInterface_SDL sysInterface;
  sysInterface.SetWindow(window);

  RmlImGuiRenderer rmlRenderer;
  rmlRenderer.set_device(device, queue);

  Rml::SetSystemInterface(&sysInterface);
  Rml::SetRenderInterface(&rmlRenderer);

  if (!Rml::Initialise()) {
    SDL_Log("Rml::Initialise() failed");
    return 1;
  }

  // Load the embedded Courier Prime Code font as "AppFont" (used by the RML).
  Rml::LoadFontFace(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code), sizeof(courier_prime_code)), "AppFont",
                    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);

  // Load italic variant so <em> elements render correctly.
  Rml::LoadFontFace(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code_italic), sizeof(courier_prime_code_italic)),
                    "AppFont", Rml::Style::FontStyle::Italic, Rml::Style::FontWeight::Normal);

  // Load monospace variant for the #diag diagnostic element.
  Rml::LoadFontFace(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code), sizeof(courier_prime_code)), "monospace",
                    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);

  Rml::Context* ctx = Rml::CreateContext("main", Rml::Vector2i(pxW, pxH));
  if (!ctx) {
    SDL_Log("Rml::CreateContext failed");
    return 1;
  }

  Rml::Debugger::Initialise(ctx);

  // Load document from the embedded string (unchanged content).
  Rml::ElementDocument* doc = ctx->LoadDocumentFromMemory(kDemoRml, "demo.rml");
  if (!doc) {
    SDL_Log("Failed to load RML document.");
    return 1;
  }
  doc->Show();

  Rml::Element* diag = doc->GetElementById("diag");
  SDL_Log("RmlUi demo started. F8 = debugger, ESC = exit.");

  // ---- Main loop --------------------------------------------------------
  bool running = true;
  while (running) {
    // --- event processing ---
    input_state.begin_frame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      RmlSDL::InputEventHandler(ctx, window, event);

      switch (event.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        case SDL_EVENT_KEY_DOWN:
          input_state.on_key(static_cast<uint32_t>(event.key.key), true);
          if (event.key.key == SDLK_ESCAPE) running = false;
          if (event.key.key == SDLK_F8) Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
          break;
        case SDL_EVENT_KEY_UP:
          input_state.on_key(static_cast<uint32_t>(event.key.key), false);
          break;
        case SDL_EVENT_MOUSE_MOTION:
          input_state.on_mouse_move(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          input_state.on_mouse_button(event.button.button, true);
          break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
          input_state.on_mouse_button(event.button.button, false);
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          input_state.on_wheel(event.wheel.y);
          break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          int w = 0, h = 0;
          SDL_GetWindowSizeInPixels(window, &w, &h);
          configureSurface(w, h);
          ctx->SetDimensions(Rml::Vector2i(w, h));
          break;
        }
        default:
          break;
      }
    }

    // --- per-frame diagnostic overlay (mirrors the original onTick logic) ---
    if (diag) {
      int logW = 0, logH = 0;
      SDL_GetWindowSize(window, &logW, &logH);
      int pxW2 = 0, pxH2 = 0;
      SDL_GetWindowSizeInPixels(window, &pxW2, &pxH2);
      const float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
      char buf[256];
      SDL_snprintf(buf, sizeof(buf),
                   "SDL_WindowSize: %dx%d\n"
                   "SDL_WindowSizeInPixels: %dx%d\n"
                   "DisplayContentScale: %.2f\n"
                   "ComputedPhys(log*scale): %.0fx%.0f",
                   logW, logH, pxW2, pxH2, scale, logW * scale, logH * scale);
      diag->SetInnerRML(buf);
    }

    // --- RmlUi layout update ---
    ctx->Update();

    // --- ImGui frame (RmlUi renders into the background draw list) ---
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ctx->Render();  // calls back into RmlImGuiRenderer -> background draw list

    ImGui::Render();

    // --- WebGPU frame --------------------------------------------------
    WGPUSurfaceTexture st = {};
    wgpuSurfaceGetCurrentTexture(surface, &st);
    if (ImGui_ImplWGPU_IsSurfaceStatusError(st.status)) {
      if (st.texture) wgpuTextureRelease(st.texture);
      SDL_Log("Fatal WebGPU surface error (%d)", static_cast<int>(st.status));
      break;
    }
    if (!st.texture) continue;  // transient (timeout / lost) — skip frame

    WGPUTextureView backbuf = wgpuTextureCreateView(st.texture, nullptr);

    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = backbuf;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {0.102, 0.102, 0.180, 1.0};  // matches #1a1a2e

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &ca;

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, nullptr);
    WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(enc, &passDesc);

    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), rp);

    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    wgpuTextureViewRelease(backbuf);
    wgpuTextureRelease(st.texture);
    wgpuSurfacePresent(surface);
  }

  // ---- Cleanup ----------------------------------------------------------
  doc->Close();
  Rml::Shutdown();

  ImGui_ImplWGPU_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  wgpuQueueRelease(queue);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(aReq.adapter);
  wgpuSurfaceUnconfigure(surface);
  wgpuSurfaceRelease(surface);
  wgpuInstanceRelease(instance);

#if defined(SDL_PLATFORM_APPLE) && !defined(__EMSCRIPTEN__)
  if (g_metal_view) SDL_Metal_DestroyView(g_metal_view);
#endif

  SDL_DestroyWindow(window);
  SDL_Quit();

  scheduler.shutdown();
  return 0;
}
