#define SDL_MAIN_HANDLED true

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_wgpu.h"
#include "Manager.h"
#include "ecs/world.hpp"
#include "jobs/scheduler.hpp"

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>
#include <cstdio>

#if defined(SDL_PLATFORM_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  include <windows.h>
#endif

// --- WebGPU global state ---
static WGPUInstance wgpu_instance = nullptr;
static WGPUDevice wgpu_device = nullptr;
static WGPUSurface wgpu_surface = nullptr;
static WGPUQueue wgpu_queue = nullptr;
static WGPUSurfaceConfiguration wgpu_surface_cfg = {};
static int wgpu_surface_width = 1280;
static int wgpu_surface_height = 800;

static void ResizeSurface(int width, int height) {
  wgpu_surface_cfg.width = wgpu_surface_width = width;
  wgpu_surface_cfg.height = wgpu_surface_height = height;
  wgpuSurfaceConfigure(wgpu_surface, &wgpu_surface_cfg);
}

static WGPUAdapter RequestAdapter(wgpu::Instance& instance) {
  wgpu::Adapter acquired;
  wgpu::RequestAdapterOptions opts;
  auto cb = [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView msg) {
    if (status != wgpu::RequestAdapterStatus::Success) {
      SDL_Log("RequestAdapter failed: %s", msg.data);
      return;
    }
    acquired = std::move(adapter);
  };
  wgpu::Future f{instance.RequestAdapter(&opts, wgpu::CallbackMode::WaitAnyOnly, cb)};
  instance.WaitAny(f, UINT64_MAX);
  return acquired.MoveToCHandle();
}

static WGPUDevice RequestDevice(wgpu::Instance& instance, wgpu::Adapter& adapter) {
  wgpu::DeviceDescriptor desc;
  desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView msg) {
    SDL_Log("WebGPU device lost (%d): %s", static_cast<int>(reason), msg.data);
  });
  desc.SetUncapturedErrorCallback(
      [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView msg) { SDL_Log("WebGPU error (%d): %s", static_cast<int>(type), msg.data); });
  wgpu::Device acquired;
  auto cb = [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView msg) {
    if (status != wgpu::RequestDeviceStatus::Success) {
      SDL_Log("RequestDevice failed: %s", msg.data);
      return;
    }
    acquired = std::move(device);
  };
  wgpu::Future f{adapter.RequestDevice(&desc, wgpu::CallbackMode::WaitAnyOnly, cb)};
  instance.WaitAny(f, UINT64_MAX);
  return acquired.MoveToCHandle();
}

#ifndef __EMSCRIPTEN__
static WGPUSurface CreateWGPUSurface(const WGPUInstance& instance, SDL_Window* window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  ImGui_ImplWGPU_CreateSurfaceInfo info = {};
  info.Instance = instance;
#  if defined(SDL_PLATFORM_MACOS)
  info.System = "cocoa";
  info.RawWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#  elif defined(SDL_PLATFORM_LINUX)
  if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
    info.System = "wayland";
    info.RawDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    info.RawSurface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
  }
  info.System = "x11";
  info.RawWindow = reinterpret_cast<void*>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
  info.RawDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#  elif defined(SDL_PLATFORM_WIN32)
  info.System = "win32";
  info.RawWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  info.RawInstance = static_cast<void*>(::GetModuleHandle(nullptr));
  return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#  else
  SDL_Log("Unsupported platform for WebGPU surface creation");
  return nullptr;
#  endif
}
#endif  // !__EMSCRIPTEN__

static bool InitWGPU(SDL_Window* window) {
  wgpu::InstanceDescriptor inst_desc = {};
  static constexpr wgpu::InstanceFeatureName kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
  inst_desc.requiredFeatureCount = 1;
  inst_desc.requiredFeatures = &kTimedWaitAny;
  wgpu::Instance instance = wgpu::CreateInstance(&inst_desc);
  if (!instance) {
    SDL_Log("Failed to create WebGPU instance");
    return false;
  }

  wgpu::Adapter adapter = RequestAdapter(instance);
  if (!adapter) return false;
  ImGui_ImplWGPU_DebugPrintAdapterInfo(adapter.Get());

  wgpu_device = RequestDevice(instance, adapter);
  if (!wgpu_device) return false;

#ifdef __EMSCRIPTEN__
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas_desc = {};
  canvas_desc.selector = "#canvas";
  wgpu::SurfaceDescriptor surf_desc = {};
  surf_desc.nextInChain = &canvas_desc;
  wgpu::Surface surface = instance.CreateSurface(&surf_desc);
#else
  wgpu::Surface surface = CreateWGPUSurface(instance.Get(), window);
#endif
  if (!surface) {
    SDL_Log("Failed to create WebGPU surface");
    return false;
  }

  wgpu_instance = instance.MoveToCHandle();
  wgpu_surface = surface.MoveToCHandle();

  WGPUSurfaceCapabilities caps = {};
  wgpuSurfaceGetCapabilities(wgpu_surface, adapter.Get(), &caps);

  wgpu_surface_cfg.presentMode = WGPUPresentMode_Fifo;
  wgpu_surface_cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
  wgpu_surface_cfg.usage = WGPUTextureUsage_RenderAttachment;
  wgpu_surface_cfg.width = wgpu_surface_width;
  wgpu_surface_cfg.height = wgpu_surface_height;
  wgpu_surface_cfg.device = wgpu_device;
  wgpu_surface_cfg.format = caps.formats[0];

  wgpuSurfaceConfigure(wgpu_surface, &wgpu_surface_cfg);
  wgpu_queue = wgpuDeviceGetQueue(wgpu_device);
  return true;
}

int main(int, char**) {
  // --- DOD World + Scheduler (replaces OOP Engine) ---
  SDL_Log("Creating DOD World");
  ecs::World world;
  jobs::Scheduler sched;
  SDL_Log("DOD World Created");

  // --- SDL init ---
  SDL_Log("Initialising SDL");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  wgpu_surface_width = static_cast<int>(wgpu_surface_width * scale);
  wgpu_surface_height = static_cast<int>(wgpu_surface_height * scale);

  SDL_Window* window = SDL_CreateWindow("Conway's Game of Life", wgpu_surface_width, wgpu_surface_height, SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return 1;
  }

  // --- WebGPU init ---
  SDL_Log("Initialising WebGPU");
  if (!InitWGPU(window)) {
    SDL_Log("InitWGPU failed");
    return 1;
  }
  SDL_Log("WebGPU Ready");

  // --- ImGui init ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();

  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;

  ImGui_ImplSDL3_InitForOther(window);

  ImGui_ImplWGPU_InitInfo wgpu_init = {};
  wgpu_init.Device = wgpu_device;
  wgpu_init.NumFramesInFlight = 3;
  wgpu_init.RenderTargetFormat = wgpu_surface_cfg.format;
  wgpu_init.DepthStencilFormat = WGPUTextureFormat_Undefined;
  ImGui_ImplWGPU_Init(&wgpu_init);

  // --- Game of Life ---
  Manager manager;
  manager.Start();
  SDL_Log("Game of Life Started");

  ImVec4 clear_color = {0.05f, 0.05f, 0.05f, 1.00f};
  bool done = false;

  while (!done) {
#ifdef __EMSCRIPTEN__
    SDL_Delay(1);  // yield to the browser event loop via asyncify (prevents busy spin)
#endif
    // --- Event processing ---
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) done = true;
    }

    // --- React to window resize ---
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (w != wgpu_surface_width || h != wgpu_surface_height) ResizeSurface(w, h);

    // --- Acquire surface texture ---
    WGPUSurfaceTexture surface_texture;
    wgpuSurfaceGetCurrentTexture(wgpu_surface, &surface_texture);
    if (ImGui_ImplWGPU_IsSurfaceStatusError(surface_texture.status)) {
      SDL_Log("Unrecoverable surface texture status=%#.8x", surface_texture.status);
      break;
    }
    if (ImGui_ImplWGPU_IsSurfaceStatusSubOptimal(surface_texture.status)) {
      if (surface_texture.texture) wgpuTextureRelease(surface_texture.texture);
      if (w > 0 && h > 0) ResizeSurface(w, h);
      continue;
    }

    // --- ImGui frame + game update ---
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    manager.Update(io.DeltaTime);
    manager.OnGui();
    manager.OnDraw();  // draws to background draw list before Render()

    ImGui::Render();

    // --- WebGPU render pass ---
    WGPUTextureViewDescriptor view_desc = {};
    view_desc.format = wgpu_surface_cfg.format;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    view_desc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    view_desc.aspect = WGPUTextureAspect_All;
    WGPUTextureView texture_view = wgpuTextureCreateView(surface_texture.texture, &view_desc);

    WGPURenderPassColorAttachment color_att = {};
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = {clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w};
    color_att.view = texture_view;

    WGPURenderPassDescriptor rp_desc = {};
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;
    rp_desc.depthStencilAttachment = nullptr;

    WGPUCommandEncoderDescriptor enc_desc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(wgpu_device, &enc_desc);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmd_desc = {};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(wgpu_queue, 1, &cmd);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(wgpu_surface);
    wgpuDeviceTick(wgpu_device);
#endif

    wgpuTextureViewRelease(texture_view);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(cmd);
  }

  // --- Cleanup ---
  SDL_Log("Exiting Game of Life");
  sched.shutdown();

  ImGui_ImplWGPU_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  wgpuSurfaceUnconfigure(wgpu_surface);
  wgpuSurfaceRelease(wgpu_surface);
  wgpuQueueRelease(wgpu_queue);
  wgpuDeviceRelease(wgpu_device);
  wgpuInstanceRelease(wgpu_instance);

  SDL_DestroyWindow(window);
  SDL_Quit();

  SDL_Log("Game of Life Exited");
  return 0;
}
