#define SDL_MAIN_HANDLED true

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_wgpu.h"
#include "ecs/world.hpp"
#include "jobs/scheduler.hpp"
#include "World.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>
#include <glm/glm.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#if defined(SDL_PLATFORM_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN 1
#  endif
#  include <windows.h>
#endif

// ============================================================
// ECS components — DOD representation of the hex grid
// ============================================================
struct HexPosition {
  glm::ivec2 gridPos;
  int linearIdx;
};

struct BlockedState {
  bool blocked = false;
};

struct AgentState {
  bool isCat = false;
  glm::ivec2 pos = {0, 0};
};

// ============================================================
// WebGPU global state (same pattern as editor/main.cpp)
// ============================================================
static WGPUInstance wgpu_instance = nullptr;
static WGPUDevice wgpu_device = nullptr;
static WGPUSurface wgpu_surface = nullptr;
static WGPUQueue wgpu_queue = nullptr;
static WGPUSurfaceConfiguration wgpu_surface_cfg = {};
static int wgpu_surface_width = 1280;
static int wgpu_surface_height = 800;

static void ResizeSurface(int w, int h) {
  wgpu_surface_cfg.width = wgpu_surface_width = w;
  wgpu_surface_cfg.height = wgpu_surface_height = h;
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

// ============================================================
// Hex rendering — ImGui background draw list replaces Renderer2D
// ============================================================
static void FillHexagon(ImDrawList* dl, float cx, float cy, float radius, ImU32 color) {
  constexpr int kSides = 6;
  constexpr float kPi = 3.14159265359f;
  cx = roundf(cx);
  cy = roundf(cy);
  for (int i = 0; i < kSides; ++i) {
    float a1 = (kPi / 3.0f) * i + kPi / 6.0f;
    float a2 = (kPi / 3.0f) * (i + 1) + kPi / 6.0f;
    ImVec2 center{cx, cy};
    ImVec2 p1{cx + cosf(a1) * radius, cy + sinf(a1) * radius};
    ImVec2 p2{cx + cosf(a2) * radius, cy + sinf(a2) * radius};
    dl->AddTriangleFilled(center, p1, p2, color);
  }
}

// Direct translation of original World::OnDraw() to ImGui draw list.
// The ECS view (via HexPosition / BlockedState) is used by the sync path;
// the draw path iterates CatWorld's contiguous worldState for correct order.
static void drawHexGrid(const CatWorld& catWorld, float winW, float winH) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  int sz = catWorld.getWorldSideSize();
  // (std::min): parentheses block the windows.h 'min' macro on MSVC/clang-cl.
  float scale = ((std::min)(winW, winH) / static_cast<float>(sz)) / 2.0f;
  float radius = floorf(scale) - 0.5f;

  float posX = winW / 2.0f - sz * scale;
  float posY = winH / 2.0f - (sz - 1) * scale;
  if (sz % 4 >= 2) posX += scale;

  const auto& state = catWorld.worldState();
  auto catPos = catWorld.getCat();
  int catIdx = (catPos.y + sz / 2) * sz + (catPos.x + sz / 2);
  int total = static_cast<int>(state.size());

  for (int i = 0; i < total;) {
    ImU32 color;
    if (i == catIdx)
      color = IM_COL32(255, 65, 65, 255);  // red   — cat
    else if (state[i])
      color = IM_COL32(65, 128, 255, 255);  // blue  — blocked
    else
      color = IM_COL32(180, 180, 180, 255);  // gray  — open

    FillHexagon(dl, posX, posY, radius, color);
    i++;

    if (i % (2 * sz) == 0) {
      posX = winW / 2.0f - sz * scale + (sz % 4 >= 2 ? 1.0f : 0.0f) * scale;
      posY += 2.0f * scale;
    } else if (i % sz == 0) {
      posX = winW / 2.0f - sz * scale + (sz % 4 <= 1 ? 1.0f : 0.0f) * scale;
      posY += 2.0f * scale;
    } else {
      posX += 2.0f * scale;
    }
  }
}

// ============================================================
// ECS sync helpers — keep DOD entities consistent with game state
// ============================================================
static void rebuildECS(ecs::World& ecsWorld, const CatWorld& catWorld, std::vector<ecs::Entity>& cells, ecs::Entity& catEntity) {
  for (auto e : cells) ecsWorld.destroy(e);
  cells.clear();
  if (ecsWorld.valid(catEntity)) ecsWorld.destroy(catEntity);

  int sz = catWorld.getWorldSideSize();
  int half = sz / 2;
  const auto& state = catWorld.worldState();
  int idx = 0;
  for (int y = -half; y <= half; ++y) {
    for (int x = -half; x <= half; ++x) {
      ecs::Entity e = ecsWorld.create();
      ecsWorld.add<HexPosition>(e, HexPosition{{x, y}, idx});
      ecsWorld.add<BlockedState>(e, BlockedState{state[idx]});
      cells.push_back(e);
      ++idx;
    }
  }
  catEntity = ecsWorld.create();
  ecsWorld.add<AgentState>(catEntity, AgentState{true, catWorld.getCat()});
}

static void syncECS(ecs::World& ecsWorld, const CatWorld& catWorld, const std::vector<ecs::Entity>& cells, ecs::Entity catEntity) {
  const auto& state = catWorld.worldState();
  for (std::size_t i = 0; i < cells.size(); ++i) ecsWorld.get<BlockedState>(cells[i]).blocked = state[i];
  if (ecsWorld.valid(catEntity)) ecsWorld.get<AgentState>(catEntity).pos = catWorld.getCat();
}

// ============================================================
// CLI helpers — preserve original headless-mode interface
// ============================================================
static void printUsage() {
  std::cout << "Usage: catchthecat [--headless --turn <cat|catcher> --size <size> --board <board_string>]\n";
  std::cout << "  --headless: Run in headless mode\n";
  std::cout << "  --turn: Specify whose turn it is (cat or catcher)\n";
  std::cout << "  --size: Size of the board (odd number)\n";
  std::cout << "  --board: Board configuration using . (empty), # (blocked), C (cat)\n";
  std::cout << "Example: catchthecat --headless --turn cat --size 5 --board \".....#....C....#.....\"\n";
}

static Point2D findCatPosition(const std::string& boardStr, int size) {
  int pos = 0;
  for (int i = 0; i < static_cast<int>(boardStr.length()); i++) {
    char c = boardStr[i];
    if (c == '.' || c == '#') {
      pos++;
      continue;
    } else if (c == 'C') {
      int y = pos / size;
      int x = pos % size;
      return {x - size / 2, y - size / 2};
    }
  }
  return {0, 0};
}

static std::vector<bool> parseBoardString(const std::string& boardStr, int size) {
  std::vector<bool> worldState(size * size, false);
  int validCharCount = 0;
  int expectedCount = size * size;

  for (int i = 0; i < static_cast<int>(boardStr.length()) && validCharCount < expectedCount; i++) {
    char c = boardStr[i];
    if (c == '#') {
      worldState[validCharCount++] = true;
    } else if (c == '.' || c == 'C') {
      worldState[validCharCount++] = false;
    }
  }

  if (validCharCount != expectedCount) {
    std::cerr << "Error: Found " << validCharCount << " valid characters, but expected " << expectedCount << " for a " << size << "x" << size
              << " board\n";
    return std::vector<bool>(size * size, false);
  }
  return worldState;
}

struct GameConfig {
  bool headless = false;
  bool isCatTurn = true;
  int size = 21;
  std::string boardStr = "";
};

static int parseCommandLineArguments(int argc, char** argv, GameConfig& config) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--headless") {
      config.headless = true;
    } else if (arg == "--turn" && i + 1 < argc) {
      std::string turn = argv[++i];
      if (turn == "cat")
        config.isCatTurn = true;
      else if (turn == "catcher")
        config.isCatTurn = false;
      else {
        std::cerr << "Error: Invalid turn value. Use 'cat' or 'catcher'\n";
        printUsage();
        return 1;
      }
    } else if (arg == "--size" && i + 1 < argc) {
      config.size = std::stoi(argv[++i]);
      if (config.size % 2 == 0 || config.size < 3) {
        std::cerr << "Error: Size must be an odd number >= 3\n";
        printUsage();
        return 1;
      }
    } else if (arg == "--board" && i + 1 < argc) {
      config.boardStr = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown argument " << arg << "\n";
      printUsage();
      return 1;
    }
  }
  return -1;  // continue
}

// ============================================================
// Headless mode: pure game logic, no window / GPU required
// ============================================================
static int runHeadlessMode(const GameConfig& config) {
  if (config.boardStr.empty()) {
    std::cerr << "Error: Board string is required for headless mode\n";
    printUsage();
    return 1;
  }

  Point2D catPos = findCatPosition(config.boardStr, config.size);
  std::vector<bool> wst = parseBoardString(config.boardStr, config.size);

  CatWorld world(config.size, config.isCatTurn, catPos, wst);
  world.step();
  world.print();
  std::cout << world.moveDuration << std::endl;
  std::cout << world.lastMove.x << "," << world.lastMove.y << std::endl;
  return 0;
}

// ============================================================
// Windowed mode: SDL3 + WebGPU + ImGui + ECS (replaces Engine)
// ============================================================
static int runRegularMode(int size) {
  // DOD bootstrap: ecs::World + jobs::Scheduler replace the OOP Engine
  SDL_Log("Creating DOD World and Scheduler");
  ecs::World ecsWorld;
  jobs::Scheduler sched;
  SDL_Log("DOD World Created");

  CatWorld catWorld(size);
  std::vector<ecs::Entity> hexCells;
  ecs::Entity catEntity = ecs::kInvalidEntity;
  rebuildECS(ecsWorld, catWorld, hexCells, catEntity);

  // SDL init
  SDL_Log("Initialising SDL");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  float uiScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  wgpu_surface_width = static_cast<int>(wgpu_surface_width * uiScale);
  wgpu_surface_height = static_cast<int>(wgpu_surface_height * uiScale);

  SDL_Window* window = SDL_CreateWindow("Catch The Cat", wgpu_surface_width, wgpu_surface_height, SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return 1;
  }

  // WebGPU init
  SDL_Log("Initialising WebGPU");
  if (!InitWGPU(window)) {
    SDL_Log("InitWGPU failed");
    return 1;
  }
  SDL_Log("WebGPU Ready");

  // ImGui init
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();

  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes(uiScale);
  style.FontScaleDpi = uiScale;

  ImGui_ImplSDL3_InitForOther(window);

  ImGui_ImplWGPU_InitInfo wgpu_init = {};
  wgpu_init.Device = wgpu_device;
  wgpu_init.NumFramesInFlight = 3;
  wgpu_init.RenderTargetFormat = wgpu_surface_cfg.format;
  wgpu_init.DepthStencilFormat = WGPUTextureFormat_Undefined;
  ImGui_ImplWGPU_Init(&wgpu_init);

  SDL_Log("Catch The Cat Started");

  ImVec4 clear_color = {0.10f, 0.10f, 0.10f, 1.00f};
  bool done = false;
  int lastSideSize = catWorld.getWorldSideSize();
  auto lastTime = std::chrono::high_resolution_clock::now();

  while (!done) {
#ifdef __EMSCRIPTEN__
    SDL_Delay(1);  // yield to the browser event loop via asyncify (prevents busy spin)
#endif
    // Event processing
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) done = true;
    }

    // Delta time
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    // Game update
    catWorld.update(dt);

    // Sync or rebuild ECS when board size changes
    if (catWorld.getWorldSideSize() != lastSideSize) {
      rebuildECS(ecsWorld, catWorld, hexCells, catEntity);
      lastSideSize = catWorld.getWorldSideSize();
    } else {
      syncECS(ecsWorld, catWorld, hexCells, catEntity);
    }

    // React to window resize
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW != wgpu_surface_width || winH != wgpu_surface_height) ResizeSurface(winW, winH);

    // Acquire surface texture
    WGPUSurfaceTexture surface_texture;
    wgpuSurfaceGetCurrentTexture(wgpu_surface, &surface_texture);
    if (ImGui_ImplWGPU_IsSurfaceStatusError(surface_texture.status)) {
      SDL_Log("Unrecoverable surface texture status=%#.8x", surface_texture.status);
      break;
    }
    if (ImGui_ImplWGPU_IsSurfaceStatusSubOptimal(surface_texture.status)) {
      if (surface_texture.texture) wgpuTextureRelease(surface_texture.texture);
      if (winW > 0 && winH > 0) ResizeSurface(winW, winH);
      continue;
    }

    // ImGui frame
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Hex grid rendered behind all ImGui windows via background draw list
    drawHexGrid(catWorld, static_cast<float>(winW), static_cast<float>(winH));

    // Settings panel (equivalent to original World::OnGui, context-param removed)
    {
      ImGui::Begin("Settings", nullptr);
      ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", io.DeltaTime * 1000.0f, 1.0f / io.DeltaTime, 1000.0f / io.Framerate, io.Framerate);

      static int newSize = catWorld.getWorldSideSize();
      if (ImGui::SliderInt("Side Size", &newSize, 5, 29)) {
        newSize = (newSize / 4) * 4 + 1;
        if (newSize != catWorld.getWorldSideSize()) catWorld.setSizeAndReset(newSize);
      }
      if (ImGui::SliderFloat("Turn Duration", &catWorld.timeBetweenAITicksRef(), 0.0f, 30.0f)
          && catWorld.getWorldSideSize() != (newSize / 2) * 2 + 1) {
        catWorld.setSizeAndReset((newSize / 2) * 2 + 1);
      }
      ImGui::Text(catWorld.isCatTurn() ? "Turn: CAT" : "Turn: CATCHER");
      ImGui::Text("Move duration: %lli", catWorld.moveDuration);
      ImGui::Text("Next turn in %.1f", catWorld.timeForNextTick());
      if (ImGui::Button("Randomize")) catWorld.randomize();
      ImGui::Text("Simulation");
      if (ImGui::Button("Step")) {
        catWorld.setSimulating(false);
        catWorld.step();
      }
      ImGui::SameLine();
      if (ImGui::Button("Start")) catWorld.setSimulating(true);
      ImGui::SameLine();
      if (ImGui::Button("Pause")) catWorld.setSimulating(false);
      ImGui::End();
    }

    // Win / loss overlay
    if (catWorld.catcherWon() || catWorld.catWon()) {
      ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      if (catWorld.catcherWon()) {
        ImGui::Begin("Catcher Won");
        if (ImGui::Button("OK", ImVec2(200, 0))) catWorld.randomize();
        ImGui::End();
      }
      if (catWorld.catWon()) {
        ImGui::Begin("Cat Won");
        if (ImGui::Button("OK", ImVec2(200, 0))) catWorld.randomize();
        ImGui::End();
      }
    }

    ImGui::Render();

    // WebGPU render pass
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

  // Cleanup
  SDL_Log("Exiting Catch The Cat");
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

  SDL_Log("Catch The Cat Exited");
  return 0;
}

// ============================================================
// Entry point
// ============================================================
int main(int argc, char** argv) {
  GameConfig config;
  int parseResult = parseCommandLineArguments(argc, argv, config);
  if (parseResult != -1) return parseResult;

  if (config.headless) {
    return runHeadlessMode(config);
  } else {
    return runRegularMode(config.size);
  }
}
