#include "Window.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

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
// Dawn's wgpuInstanceRequestAdapter is async even on native. We use a tiny
// polling loop with wgpuInstanceProcessEvents() to block until the callback
// fires. On the web (emdawnwebgpu) the same scheme works thanks to Asyncify
// (emscripten_sleep yields to the JS event loop).
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

static void pumpUntil(WGPUInstance instance, bool& flag) {
  while (!flag) {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(1);
#else
    wgpuInstanceProcessEvents(instance);
#endif
  }
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

  int width  = 1280;
  int height = 720;
#ifdef __EMSCRIPTEN__
  width  = canvas_get_width();
  height = canvas_get_height();
#endif

  const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  sdlWindow = SDL_CreateWindow(title.c_str(), width, height, flags);
  if (!sdlWindow) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    throw std::runtime_error("SDL_CreateWindow failed");
  }
  SDL_Log("SDL3 window created (%dx%d)", width, height);

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
  wgpuInstance = wgpuCreateInstance(&instDesc);
  if (!wgpuInstance) {
    throw std::runtime_error("wgpuCreateInstance failed");
  }

  createSurface();
  initDeviceAndQueue();

  // Now we know `surfaceFormat` and have a device; configure the surface.
  int pxW = 0, pxH = 0;
  SDL_GetWindowSizeInPixels(sdlWindow, &pxW, &pxH);
  int logW = 0, logH = 0;
  SDL_GetWindowSize(sdlWindow, &logW, &logH);
  windowSize = {logW, logH};
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
  aCb.mode      = WGPUCallbackMode_AllowProcessEvents;
  aCb.callback  = onAdapter;
  aCb.userdata1 = &aReq;
  wgpuInstanceRequestAdapter(wgpuInstance, &aOpts, aCb);
  pumpUntil(wgpuInstance, aReq.done);
  if (!aReq.adapter) throw std::runtime_error("No WebGPU adapter available");
  wgpuAdapter = aReq.adapter;

  // Request device.
  DeviceReq dReq;
  WGPUDeviceDescriptor dDesc = {};
  dDesc.label = {"mobagen device", WGPU_STRLEN};
  dDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;

  WGPURequestDeviceCallbackInfo dCb = {};
  dCb.mode      = WGPUCallbackMode_AllowProcessEvents;
  dCb.callback  = onDevice;
  dCb.userdata1 = &dReq;
  wgpuAdapterRequestDevice(wgpuAdapter, &dDesc, dCb);
  pumpUntil(wgpuInstance, dReq.done);
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
  int pxW = 0, pxH = 0;
  SDL_GetWindowSizeInPixels(sdlWindow, &pxW, &pxH);
  int logW = 0, logH = 0;
  SDL_GetWindowSize(sdlWindow, &logW, &logH);

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
  }
}

Window::~Window() {
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
