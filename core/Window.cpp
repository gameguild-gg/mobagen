#include "Window.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include "RmlUiWgpuRenderer.h"
#include "RmlUi_Platform_SDL.h"
#include <RmlUi/Debugger/FontSource.h>

#include <cstdio>
#include <stdexcept>
#include <string>

#if defined(SDL_PLATFORM_APPLE)
#  include <SDL3/SDL_metal.h>
#endif

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  include <emscripten/html5.h>
EM_JS(int, canvas_get_width,  (), { return canvas.width;  });
EM_JS(int, canvas_get_height, (), { return canvas.height; });
#endif

// ---------------------------------------------------------------------------
// Adapter / device acquisition
//
// Dawn's wgpuInstanceRequestAdapter is async even on native. We use
// WGPUCallbackMode_WaitAnyOnly and block on wgpuInstanceWaitAny, which works
// uniformly on native (polling) and on Emscripten (yields to the JS event
// loop via Asyncify).  On Emscripten emdawnwebgpu's wgpuInstanceWaitAny is
// implemented in terms of Promise.race, so a timeout of UINT64_MAX is
// equivalent to "wait forever" but lets the JS event loop make progress.
// ---------------------------------------------------------------------------

namespace {

struct AdapterReq {
  WGPUAdapter adapter = nullptr;
  bool        done    = false;
};

static void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                      WGPUStringView message, void* userdata1, void* /*userdata2*/) {
  auto* req = static_cast<AdapterReq*>(userdata1);
  if (status == WGPURequestAdapterStatus_Success) {
    req->adapter = adapter;
  } else {
    SDL_Log("wgpuInstanceRequestAdapter failed: %.*s",
            (int)message.length, message.data ? message.data : "");
  }
  req->done = true;
}

struct DeviceReq {
  WGPUDevice device = nullptr;
  bool       done   = false;
};

static void onDevice(WGPURequestDeviceStatus status, WGPUDevice device,
                     WGPUStringView message, void* userdata1, void* /*userdata2*/) {
  auto* req = static_cast<DeviceReq*>(userdata1);
  if (status == WGPURequestDeviceStatus_Success) {
    req->device = device;
  } else {
    SDL_Log("wgpuAdapterRequestDevice failed: %.*s",
            (int)message.length, message.data ? message.data : "");
  }
  req->done = true;
}

static void onUncapturedError(WGPUDevice const* /*device*/, WGPUErrorType type,
                              WGPUStringView message, void* /*ud1*/, void* /*ud2*/) {
  SDL_Log("[WGPU error type=%d]: %.*s", (int)type,
          (int)message.length, message.data ? message.data : "");
}

} // namespace

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

Window::Window(std::string title) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    throw std::runtime_error(SDL_GetError());
  }
  SDL_Log("SDL3 initialized");

  int width  = 1280, height = 720;
  {
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display) {
      SDL_Rect bounds = {};
      // Use SDL_GetDisplayBounds (full display) instead of
      // SDL_GetDisplayUsableBounds so we get the entire device screen,
      // not a sub-area that might exclude status bar / safe areas.
      if (SDL_GetDisplayBounds(display, &bounds) && bounds.w > 0 && bounds.h > 0) {
        width  = bounds.w;
        height = bounds.h;
      }
    }
  }
#ifdef __EMSCRIPTEN__
  { // Emscripten canvas overrides display bounds
    int cw = canvas_get_width(), ch = canvas_get_height();
    if (cw > 0 && ch > 0) { width = cw; height = ch; }
  }
#endif

#if defined(SDL_PLATFORM_IOS)
  // iOS: create a full-screen window using the native display resolution.
  // Passing width=0, height=0 with SDL_WINDOW_FULLSCREEN tells SDL3 to
  // use the native display size. This ensures the Metal view (CAMetalLayer)
  // covers the entire device screen — without it, the view may be smaller
  // on the iOS simulator, causing the WebGPU drawable to not be full-screen.
  const SDL_WindowFlags flags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  sdlWindow = SDL_CreateWindow(title.c_str(), 0, 0, flags);
#else
  const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  sdlWindow = SDL_CreateWindow(title.c_str(), width, height, flags);
#endif
  if (!sdlWindow) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    throw std::runtime_error("SDL_CreateWindow failed");
  }
  SDL_Log("SDL3 window created");

  // ImGui first so backends have a context to bind to.
  IMGUI_CHECKVERSION();
  imGuiContext = ImGui::CreateContext();
  ImGui::SetCurrentContext(imGuiContext);
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  // WebGPU: instance -> adapter -> device -> queue.
  WGPUInstanceDescriptor instDesc = {};
  // Enable TimedWaitAny so that wgpuInstanceWaitAny(... > 0) actually
  // works. Without this, the Emscripten port logs a warning and aborts
  // the program whenever a synchronous wait is requested with a non-zero
  // timeout. UINT64_MAX (used as "wait forever" below) is a non-zero
  // timeout, so this is required.
  WGPUInstanceFeatureName requiredFeatures[] = {
    WGPUInstanceFeatureName_TimedWaitAny,
  };
  instDesc.requiredFeatureCount = sizeof(requiredFeatures) / sizeof(requiredFeatures[0]);
  instDesc.requiredFeatures     = requiredFeatures;
  wgpuInstance = wgpuCreateInstance(&instDesc);
  if (!wgpuInstance) {
    throw std::runtime_error("wgpuCreateInstance failed");
  }

  createSurface();
  initDeviceAndQueue();

  // Now we know `surfaceFormat` and have a device; configure the surface.
  int logW = 0, logH = 0;
  SDL_GetWindowSize(sdlWindow, &logW, &logH);
  windowSize = {logW, logH};
  // Compute physical pixel size from logical size * display content scale.
  // SDL_GetWindowSizeInPixels can return incorrect values on some platforms
  // (e.g. iOS simulator when HIGH_PIXEL_DENSITY doesn't work), so we compute
  // it manually from the display's content scale factor instead.
  float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(sdlWindow));
  int pxW = static_cast<int>(logW * scale + 0.5f);
  int pxH = static_cast<int>(logH * scale + 0.5f);
  configureSurface(pxW, pxH);
  // Set initial font scale based on logical size (same formula as Update()).
  const int minDim = logW < logH ? logW : logH;
  ImGui::GetIO().FontGlobalScale = static_cast<float>(minDim) / 500.f;

  // Wire ImGui's SDL3 + WGPU backends.
  ImGui_ImplSDL3_InitForOther(sdlWindow);

  ImGui_ImplWGPU_InitInfo wgpuInit = {};
  wgpuInit.Device             = wgpuDevice;
  wgpuInit.NumFramesInFlight  = 3;
  wgpuInit.RenderTargetFormat = surfaceFormat;
  wgpuInit.DepthStencilFormat = WGPUTextureFormat_Undefined;
  if (!ImGui_ImplWGPU_Init(&wgpuInit)) {
    throw std::runtime_error("ImGui_ImplWGPU_Init failed");
  }
}

void Window::createSurface() {
  WGPUSurfaceDescriptor desc = {};

#if defined(__EMSCRIPTEN__)
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
  canvasDesc.selector    = {"#canvas", WGPU_STRLEN};
  desc.nextInChain       = &canvasDesc.chain;
  wgpuSurface            = wgpuInstanceCreateSurface(wgpuInstance, &desc);

#elif defined(SDL_PLATFORM_APPLE)
  metalView = SDL_Metal_CreateView(sdlWindow);
  if (!metalView) {
    throw std::runtime_error(std::string("SDL_Metal_CreateView failed: ") + SDL_GetError());
  }
  void* layer = SDL_Metal_GetLayer(metalView);

  WGPUSurfaceSourceMetalLayer metalDesc = {};
  metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
  metalDesc.layer       = layer;
  desc.nextInChain      = &metalDesc.chain;
  wgpuSurface           = wgpuInstanceCreateSurface(wgpuInstance, &desc);

#elif defined(SDL_PLATFORM_WIN32)
  HWND hwnd = (HWND)SDL_GetPointerProperty(
      SDL_GetWindowProperties(sdlWindow),
      SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  HINSTANCE hinst = (HINSTANCE)SDL_GetPointerProperty(
      SDL_GetWindowProperties(sdlWindow),
      SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  WGPUSurfaceSourceWindowsHWND hwndDesc = {};
  hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
  hwndDesc.hinstance   = hinst;
  hwndDesc.hwnd        = hwnd;
  desc.nextInChain     = &hwndDesc.chain;
  wgpuSurface          = wgpuInstanceCreateSurface(wgpuInstance, &desc);

#elif defined(SDL_PLATFORM_LINUX)
  // X11 path (Wayland intentionally disabled in external/dawn.cmake).
  void* xdisplay = SDL_GetPointerProperty(
      SDL_GetWindowProperties(sdlWindow),
      SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  uint64_t xwindow = (uint64_t)SDL_GetNumberProperty(
      SDL_GetWindowProperties(sdlWindow),
      SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  WGPUSurfaceSourceXlibWindow xlibDesc = {};
  xlibDesc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
  xlibDesc.display     = xdisplay;
  xlibDesc.window      = xwindow;
  desc.nextInChain     = &xlibDesc.chain;
  wgpuSurface          = wgpuInstanceCreateSurface(wgpuInstance, &desc);

#elif defined(SDL_PLATFORM_ANDROID)
  // Android exposes the native window via the SDL_WindowProperties. The
  // WebGPU surface is then created from that ANativeWindow using Dawn's
  // Android surface source.
  void* nativeWindow = SDL_GetPointerProperty(
      SDL_GetWindowProperties(sdlWindow),
      SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
  if (!nativeWindow) {
    throw std::runtime_error(std::string(
      "SDL_GetPointerProperty(ANDROID_WINDOW) failed: ") + SDL_GetError());
  }

  WGPUSurfaceSourceAndroidNativeWindow androidDesc = {};
  androidDesc.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
  androidDesc.window      = nativeWindow;
  desc.nextInChain        = &androidDesc.chain;
  wgpuSurface             = wgpuInstanceCreateSurface(wgpuInstance, &desc);

#else
#  error "Unsupported platform for WebGPU surface creation"
#endif

  if (!wgpuSurface) {
    throw std::runtime_error("wgpuInstanceCreateSurface failed");
  }
}

void Window::initDeviceAndQueue() {
  // Request adapter (compatible with our surface so the device works with it).
  AdapterReq aReq;
  WGPURequestAdapterOptions aOpts = {};
  aOpts.compatibleSurface  = wgpuSurface;
  aOpts.powerPreference    = WGPUPowerPreference_HighPerformance;

  WGPURequestAdapterCallbackInfo aCb = {};
  aCb.mode      = WGPUCallbackMode_WaitAnyOnly;
  aCb.callback  = onAdapter;
  aCb.userdata1 = &aReq;
  WGPUFuture aFuture = wgpuInstanceRequestAdapter(wgpuInstance, &aOpts, aCb);

  WGPUFutureWaitInfo aWait = {};
  aWait.future    = aFuture;
  aWait.completed = WGPU_FALSE;
  wgpuInstanceWaitAny(wgpuInstance, 1, &aWait, UINT64_MAX);

  if (!aReq.adapter) throw std::runtime_error("No WebGPU adapter available");
  wgpuAdapter = aReq.adapter;

  // Request device.
  DeviceReq dReq;
  WGPUDeviceDescriptor dDesc = {};
  dDesc.label = {"mobagen device", WGPU_STRLEN};
  dDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;

  WGPURequestDeviceCallbackInfo dCb = {};
  dCb.mode      = WGPUCallbackMode_WaitAnyOnly;
  dCb.callback  = onDevice;
  dCb.userdata1 = &dReq;
  WGPUFuture dFuture = wgpuAdapterRequestDevice(wgpuAdapter, &dDesc, dCb);

  WGPUFutureWaitInfo dWait = {};
  dWait.future    = dFuture;
  dWait.completed = WGPU_FALSE;
  wgpuInstanceWaitAny(wgpuInstance, 1, &dWait, UINT64_MAX);

  if (!dReq.device) throw std::runtime_error("WebGPU device request failed");
  wgpuDevice = dReq.device;

  wgpuQueue = wgpuDeviceGetQueue(wgpuDevice);

  // Pick a surface format the adapter supports for our surface.
  WGPUSurfaceCapabilities caps = {};
  wgpuSurfaceGetCapabilities(wgpuSurface, wgpuAdapter, &caps);
  if (caps.formatCount > 0 && caps.formats) {
    surfaceFormat = caps.formats[0];
  } else {
    // Reasonable fallback that virtually every adapter supports.
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
  }
  wgpuSurfaceCapabilitiesFreeMembers(caps);

  SDL_Log("WebGPU device ready (surfaceFormat=%d)", (int)surfaceFormat);
}

void Window::configureSurface(int widthPx, int heightPx) {
  if (widthPx <= 0 || heightPx <= 0) return;

  WGPUSurfaceConfiguration cfg = {};
  cfg.device      = wgpuDevice;
  cfg.format      = (WGPUTextureFormat)surfaceFormat;
  cfg.usage       = WGPUTextureUsage_RenderAttachment;
  cfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
  cfg.width       = (uint32_t)widthPx;
  cfg.height      = (uint32_t)heightPx;
  cfg.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(wgpuSurface, &cfg);
}

void Window::Update() {
  int logW = 0, logH = 0;
  SDL_GetWindowSize(sdlWindow, &logW, &logH);
  // Compute physical pixel size from logical size * display content scale.
  float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(sdlWindow));
  int pxW = static_cast<int>(logW * scale + 0.5f);
  int pxH = static_cast<int>(logH * scale + 0.5f);

  Point2D logSize{logW, logH};
  if (windowSize != logSize) {
    windowSize = logSize;
    // WebGPU surface is configured at device pixels (HiDPI-aware).
    configureSurface(pxW, pxH);
    // Font scale is based on logical units so it stays consistent across DPI.
    const int minDim = logW < logH ? logW : logH;
    if (imGuiContext) {
      ImGui::GetIO().FontGlobalScale = static_cast<float>(minDim) / 500.f;
    }
    // RmlUi context resize
    if (rmlContext) {
      rmlContext->SetDimensions(Rml::Vector2i(pxW, pxH));
    }
  }
}

Window::~Window() {
  // Tear down RmlUi in the proper order:
  //   1. Remove the context (triggers Release* calls on the render interface).
  //   2. Shutdown RmlUi (releases all global resources, drops references to
  //      our system/render interfaces).
  //   3. Only then delete the system and render interfaces (RmlUi no longer
  //      references them).
  if (rmlContext) {
    Rml::RemoveContext("main");
    rmlContext = nullptr;
  }
  Rml::Shutdown();
  if (rmlSystemInterface) {
    delete static_cast<SystemInterface_SDL*>(rmlSystemInterface);
    rmlSystemInterface = nullptr;
  }
  if (rmlRenderer) {
    delete rmlRenderer;
    rmlRenderer = nullptr;
  }

  ImGui_ImplWGPU_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(imGuiContext);
  imGuiContext = nullptr;

  if (wgpuQueue)    { wgpuQueueRelease(wgpuQueue);       wgpuQueue    = nullptr; }
  if (wgpuDevice)   { wgpuDeviceRelease(wgpuDevice);     wgpuDevice   = nullptr; }
  if (wgpuAdapter)  { wgpuAdapterRelease(wgpuAdapter);   wgpuAdapter  = nullptr; }
  if (wgpuSurface)  { wgpuSurfaceUnconfigure(wgpuSurface);
                      wgpuSurfaceRelease(wgpuSurface);   wgpuSurface  = nullptr; }
  if (wgpuInstance) { wgpuInstanceRelease(wgpuInstance); wgpuInstance = nullptr; }

#if defined(SDL_PLATFORM_APPLE)
  if (metalView) { SDL_Metal_DestroyView(metalView); metalView = nullptr; }
#endif

  if (sdlWindow) { SDL_DestroyWindow(sdlWindow); sdlWindow = nullptr; }
  SDL_Quit();
}

// ---------------------------------------------------------------------------
// RmlUi initialization
// ---------------------------------------------------------------------------

void Window::InitRmlUi() {
  // Create the SDL system interface (from RmlUi's built-in SDL platform backend).
  // RmlUi takes a non-owning pointer; we own and free it in ~Window.
  auto* system = new SystemInterface_SDL();
  system->SetWindow(sdlWindow);
  rmlSystemInterface = system;
  Rml::SetSystemInterface(system);

  // Initialize RmlUi. A false return means the font engine (or another
  // global) could not initialize; subsequent Rml::CreateContext() will
  // return nullptr and the user would otherwise just see a black window.
  if (!Rml::Initialise()) {
    SDL_Log("Window: Rml::Initialise() failed — RmlUi will not be available.");
    return;
  }

  // Create the WebGPU renderer
  rmlRenderer = new RmlUiWgpuRenderer(wgpuDevice, wgpuQueue, surfaceFormat);

  // Install our render interface
  Rml::SetRenderInterface(rmlRenderer);

  // Create main context sized to the physical (drawable) pixel dimensions.
  // RmlUi is DPI-aware via SystemInterface_SDL::GetDpi(), so the context
  // dimensions must match the physical framebuffer size to render correctly
  // on Retina/HiDPI displays.
  int logW = 0, logH = 0;
  SDL_GetWindowSize(sdlWindow, &logW, &logH);
  float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(sdlWindow));
  int pxW = static_cast<int>(logW * scale + 0.5f);
  int pxH = static_cast<int>(logH * scale + 0.5f);
  rmlContext = Rml::CreateContext("main",
      Rml::Vector2i(pxW, pxH));

  // Load default font from RmlUi's own embedded font (Courier Prime Code).
  // The data lives in RmlUi's debugger FontSource.h — exposed through a
  // generated forwarding header at <RmlUi/Debugger/FontSource.h> (see
  // external/rmlui.cmake). Already compiled into rmlui_debugger, so no
  // extra font files in our repo, no filesystem access needed (works on
  // WebAssembly too). We register it as a regular font family so demos
  // can use it via CSS font-family.
  bool font_ok = true;
  {
    using namespace Rml;
    const Span<const byte> data_reg(courier_prime_code, sizeof(courier_prime_code));
    const Span<const byte> data_it(courier_prime_code_italic, sizeof(courier_prime_code_italic));

    bool r1 = LoadFontFace(data_reg, "AppFont", Style::FontStyle::Normal, Style::FontWeight::Normal);
    bool r2 = LoadFontFace(data_it,  "AppFont", Style::FontStyle::Italic, Style::FontWeight::Normal);
    font_ok = r1 || r2;
  }
  if (!font_ok) {
    SDL_Log("RmlUi: WARNING — embedded font failed to load. Text will not render.");
  }

  // Debugger (optional, helps during development)
  Rml::Debugger::Initialise(rmlContext);

  SDL_Log("RmlUi initialized (context %dx%d physical px)", pxW, pxH);
}
