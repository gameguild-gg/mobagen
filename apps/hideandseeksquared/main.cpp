#define SDL_MAIN_HANDLED

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  include <emscripten/html5.h>
#endif

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

#include "ecs/world.hpp"
#include "jobs/scheduler.hpp"
#include "Manager.h"

#include <cstdio>
#include <cstring>

#if defined(SDL_PLATFORM_APPLE)
#  include <SDL3/SDL_metal.h>
#endif

// ---------------------------------------------------------------------------
// WebGPU async request helpers — same pattern as dicom_viewer
// ---------------------------------------------------------------------------
struct AdapterReq {
  WGPUAdapter adapter = nullptr;
  bool done = false;
};
static void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void* ud1, void*) {
  auto* r = static_cast<AdapterReq*>(ud1);
  if (status == WGPURequestAdapterStatus_Success)
    r->adapter = adapter;
  else
    SDL_Log("RequestAdapter failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
  r->done = true;
}

struct DeviceReq {
  WGPUDevice device = nullptr;
  bool done = false;
};
static void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void* ud1, void*) {
  auto* r = static_cast<DeviceReq*>(ud1);
  if (status == WGPURequestDeviceStatus_Success)
    r->device = device;
  else
    SDL_Log("RequestDevice failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
  r->done = true;
}

static void onUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
  SDL_Log("[WGPU error type=%d]: %.*s", (int)type, (int)msg.length, msg.data ? msg.data : "");
}

static bool pumpUntil(WGPUInstance inst, bool& flag, const char* operation, Uint64 timeoutMs = 10000) {
  const Uint64 start = SDL_GetTicks();
  while (!flag) {
    wgpuInstanceProcessEvents(inst);
    if (SDL_GetTicks() - start > timeoutMs) {
      SDL_Log("%s timed out after %llu ms", operation, (unsigned long long)timeoutMs);
      return false;
    }
#ifdef __EMSCRIPTEN__
    emscripten_sleep(1);
#else
    SDL_Delay(1);
#endif
  }
  return true;
}

// Measure real frame delta; clamp at 100 ms to avoid huge stall spikes.
static float measure_delta_seconds() {
  static uint64_t last = SDL_GetPerformanceCounter();
  const uint64_t now = SDL_GetPerformanceCounter();
  float dt = static_cast<float>(static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency()));
  last = now;
  if (dt > 0.1f) dt = 0.1f;
  return dt;
}

// ---------------------------------------------------------------------------
// App — owns the window, WebGPU context, ImGui, ECS world, and Manager.
// ---------------------------------------------------------------------------
struct App {
  SDL_Window* window = nullptr;
  WGPUInstance instance = nullptr;
  WGPUAdapter adapter = nullptr;
  WGPUDevice device = nullptr;
  WGPUQueue queue = nullptr;
  WGPUSurface surface = nullptr;
  WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
#if defined(SDL_PLATFORM_APPLE)
  SDL_MetalView metalView = nullptr;
#endif
  int cfgW = 0, cfgH = 0;
  bool running = true;

  // DOD building blocks
  ecs::World world;
  jobs::Scheduler sched;

  // Application system (operates on Grid2D data; no Engine* dependency)
  Manager manager;

  bool init();
  void tick();
  void cleanup();

private:
  void createSurface();
  bool initDeviceAndQueue();
  void configureSurface(int w, int h);
};

void App::createSurface() {
  WGPUSurfaceDescriptor desc = {};
#if defined(__EMSCRIPTEN__)
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
  canvasDesc.selector = {"#canvas", WGPU_STRLEN};
  desc.nextInChain = &canvasDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_WIN32)
  WGPUSurfaceSourceWindowsHWND hwndDesc = {};
  hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
  hwndDesc.hinstance = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  hwndDesc.hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  desc.nextInChain = &hwndDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_APPLE)
  metalView = SDL_Metal_CreateView(window);
  WGPUSurfaceSourceMetalLayer metalDesc = {};
  metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
  metalDesc.layer = SDL_Metal_GetLayer(metalView);
  desc.nextInChain = &metalDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_LINUX)
  void* xdisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  uint64_t xwindow = (uint64_t)SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  WGPUSurfaceSourceXlibWindow xlibDesc = {};
  xlibDesc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
  xlibDesc.display = xdisplay;
  xlibDesc.window = xwindow;
  desc.nextInChain = &xlibDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#else
#  error "Unsupported platform for WebGPU surface creation"
#endif
  if (!surface) fprintf(stderr, "wgpuInstanceCreateSurface failed\n");
}

bool App::initDeviceAndQueue() {
  AdapterReq aReq;
  WGPURequestAdapterOptions aOpts = {};
  aOpts.compatibleSurface = surface;
  aOpts.powerPreference = WGPUPowerPreference_HighPerformance;
  WGPURequestAdapterCallbackInfo aCb = {};
  aCb.mode = WGPUCallbackMode_AllowProcessEvents;
  aCb.callback = onAdapter;
  aCb.userdata1 = &aReq;
  wgpuInstanceRequestAdapter(instance, &aOpts, aCb);
  if (!pumpUntil(instance, aReq.done, "requestAdapter")) return false;
  if (!aReq.adapter) {
    fprintf(stderr, "No WebGPU adapter available\n");
    return false;
  }
  adapter = aReq.adapter;

  DeviceReq dReq;
  WGPUDeviceDescriptor dDesc = {};
  dDesc.label = {"hideandseeksquared device", WGPU_STRLEN};
  dDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
  WGPURequestDeviceCallbackInfo dCb = {};
  dCb.mode = WGPUCallbackMode_AllowProcessEvents;
  dCb.callback = onDevice;
  dCb.userdata1 = &dReq;
  wgpuAdapterRequestDevice(adapter, &dDesc, dCb);
  if (!pumpUntil(instance, dReq.done, "requestDevice")) return false;
  if (!dReq.device) {
    fprintf(stderr, "WebGPU device request failed\n");
    return false;
  }
  device = dReq.device;
  queue = wgpuDeviceGetQueue(device);

  WGPUSurfaceCapabilities caps = {};
  wgpuSurfaceGetCapabilities(surface, adapter, &caps);
  surfaceFormat = (caps.formatCount > 0 && caps.formats) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
  wgpuSurfaceCapabilitiesFreeMembers(caps);
  printf("WebGPU device ready (surfaceFormat=%d)\n", (int)surfaceFormat);
  return true;
}

void App::configureSurface(int w, int h) {
  if (w <= 0 || h <= 0) return;
  WGPUSurfaceConfiguration cfg = {};
  cfg.device = device;
  cfg.format = surfaceFormat;
  cfg.usage = WGPUTextureUsage_RenderAttachment;
  cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
  cfg.width = (uint32_t)w;
  cfg.height = (uint32_t)h;
  cfg.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(surface, &cfg);
  cfgW = w;
  cfgH = h;
}

bool App::init() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  window = SDL_CreateWindow("Hide and Seek Squared", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }

  instance = wgpuCreateInstance(nullptr);
  if (!instance) {
    fprintf(stderr, "wgpuCreateInstance failed\n");
    return false;
  }

  createSurface();
  if (!initDeviceAndQueue()) return false;

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  configureSurface(w, h);

  // ImGui setup
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOther(window);

  ImGui_ImplWGPU_InitInfo wgpuInfo = {};
  wgpuInfo.Device = device;
  wgpuInfo.RenderTargetFormat = surfaceFormat;
  wgpuInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
  wgpuInfo.NumFramesInFlight = 3;
  ImGui_ImplWGPU_Init(&wgpuInfo);

  manager.Start();

  printf("hideandseeksquared initialized\n");
  return true;
}

void App::tick() {
  // Reconfigure surface on window resize
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  if (w != cfgW || h != cfgH) {
    ImGui_ImplWGPU_InvalidateDeviceObjects();
    configureSurface(w, h);
    ImGui_ImplWGPU_CreateDeviceObjects();
  }

  // Poll SDL events
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) running = false;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
  }

  // Begin ImGui frame
  ImGui_ImplWGPU_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  // App systems: controls (ImGui window), logic, grid rendering (background draw list)
  float dt = measure_delta_seconds();
  manager.OnGui();
  manager.Update(dt);
  manager.OnDraw();

  ImGui::Render();

  // Acquire the current surface texture
  WGPUSurfaceTexture surfTex = {};
  wgpuSurfaceGetCurrentTexture(surface, &surfTex);
  if (!surfTex.texture) return;

  WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  viewDesc.format = surfaceFormat;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.mipLevelCount = 1;
  viewDesc.arrayLayerCount = 1;
  viewDesc.usage = WGPUTextureUsage_RenderAttachment;
  WGPUTextureView view = wgpuTextureCreateView(surfTex.texture, &viewDesc);

  WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  colorAttach.view = view;
  colorAttach.loadOp = WGPULoadOp_Clear;
  colorAttach.storeOp = WGPUStoreOp_Store;
  colorAttach.clearValue = {0.08f, 0.08f, 0.08f, 1.0f};

  WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  passDesc.colorAttachmentCount = 1;
  passDesc.colorAttachments = &colorAttach;

  WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encDesc.label = {"frame encoder", WGPU_STRLEN};
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
  ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);

  WGPUCommandBufferDescriptor cmdDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  cmdDesc.label = {"frame commands", WGPU_STRLEN};
  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmdDesc);
  wgpuCommandEncoderRelease(encoder);

  wgpuQueueSubmit(queue, 1, &cmd);
  wgpuCommandBufferRelease(cmd);

  wgpuTextureViewRelease(view);
  wgpuTextureRelease(surfTex.texture);
  wgpuSurfacePresent(surface);
  wgpuInstanceProcessEvents(instance);
}

void App::cleanup() {
  ImGui_ImplWGPU_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  if (surface) wgpuSurfaceUnconfigure(surface);
  if (surface) wgpuSurfaceRelease(surface);
  if (queue) wgpuQueueRelease(queue);
  if (device) wgpuDeviceRelease(device);
  if (adapter) wgpuAdapterRelease(adapter);
  if (instance) wgpuInstanceRelease(instance);
#if defined(SDL_PLATFORM_APPLE)
  if (metalView) SDL_Metal_DestroyView(metalView);
#endif
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
}

#ifdef __EMSCRIPTEN__
static App* g_app = nullptr;
static void em_main_loop() {
  if (g_app && g_app->running)
    g_app->tick();
  else if (g_app)
    emscripten_cancel_main_loop();
}
#endif

int main(int, char**) {
  App app;
  if (!app.init()) return 1;

#ifdef __EMSCRIPTEN__
  g_app = &app;
  emscripten_set_main_loop(em_main_loop, 0, 1);
#else
  while (app.running) app.tick();
#endif

  app.sched.shutdown();
  app.cleanup();
  return 0;
}
