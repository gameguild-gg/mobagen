#include "Engine.h"

#include "../Polygon.h"
#include "../Renderer2D.h"
#include "../scene/GameObject.h"
#include "../scene/ScriptableObject.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

// RmlUi platform headers (used for event processing)
#include "RmlUi_Platform_SDL.h"
#include <RmlUi/Core.h>
#include "../RmlUiWgpuRenderer.h"

using namespace std::chrono_literals;

Engine::Engine(EngineSettings settings) : window(nullptr), settings(settings) {
  if (!settings.headless) {
    SDL_Log("Engine Created");
  }
  // Mirror the user-supplied clear color into our cached array so the
  // per-frame render pass descriptor can read it directly.
  for (int i = 0; i < 4; ++i) clearColor[i] = settings.clearColor[i];
  instance = this;
}

Engine::~Engine() {
  if (window) {
    // ImGui backend shutdown happens inside Window's destructor.
    delete window;
    window = nullptr;
  }

  for (auto go : gameObjects) delete go;
  gameObjects.clear();
  SDL_Log("Game Objects Cleared");
}

// https://gafferongames.com/post/fix_your_timestep/
void Engine::Run() {
  for (;;) {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count();
    deltaTime = duration / 1000000.0f;
    accumulatedTime += duration;
    lastFrameTime = currentTime;

    int64_t targetTimeToSleep = 1000000 / targetFPS;
    for (; accumulatedTime >= targetTimeToSleep; accumulatedTime -= targetTimeToSleep) {
      Tick();
      if (done) return;
    }
    if (targetTimeToSleep >= accumulatedTime)
      targetTimeToSleep -= accumulatedTime;
    else
      targetTimeToSleep = 0;
#ifdef __EMSCRIPTEN__
    emscripten_sleep(targetTimeToSleep / 1000);
#else
    SDL_Delay((Uint32)(targetTimeToSleep / 1000));
#endif
  }
}

bool Engine::Start(std::string title) {
  if (!settings.headless) {
    SDL_Log("Initializing Window");
    try {
      window = new Window(title);
    } catch (const std::exception& e) {
      SDL_Log("Window creation failed: %s", e.what());
      return false;
    }
    SDL_Log("Window Initialized");

    // Initialize RmlUi if requested
    if (settings.useRmlUi) {
      window->InitRmlUi();
    }
  } else {
    SDL_Log("Starting in headless mode - no window created");
    window = nullptr;
  }

  lastFrameTime = std::chrono::high_resolution_clock::now();
  SDL_Delay((Uint32)(1000 / targetFPS));
  deltaTime = 0;
  return true;
}

void Engine::Tick() {
  WGPUSurfaceTexture     surfaceTex     = {};
  WGPUTextureView        backbufferView = nullptr;
  WGPUCommandEncoder     encoder        = nullptr;
  WGPURenderPassEncoder  pass           = nullptr;
  // Physical pixel dimensions — needed for HiDPI-correct scissor rects when
  // using RmlUi. On non-HiDPI displays this equals the logical window size.
  int physW = 0, physH = 0;

  if (!settings.headless) {
    window->Update();
    SDL_GetWindowSizeInPixels(window->sdlWindow, &physW, &physH);
    if (physW <= 0 || physH <= 0) { physW = window->size().x; physH = window->size().y; }

    wgpuSurfaceGetCurrentTexture(window->wgpuSurface, &surfaceTex);
    bool haveBackbuffer =
        (surfaceTex.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
         surfaceTex.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) &&
        surfaceTex.texture != nullptr;

    if (haveBackbuffer) {
      WGPUTextureViewDescriptor vd = {};
      vd.format          = wgpuTextureGetFormat(surfaceTex.texture);
      vd.dimension       = WGPUTextureViewDimension_2D;
      vd.baseMipLevel    = 0;
      vd.mipLevelCount   = 1;
      vd.baseArrayLayer  = 0;
      vd.arrayLayerCount = 1;
      vd.aspect          = WGPUTextureAspect_All;
      backbufferView     = wgpuTextureCreateView(surfaceTex.texture, &vd);

      WGPUCommandEncoderDescriptor edesc = {};
      encoder = wgpuDeviceCreateCommandEncoder(window->wgpuDevice, &edesc);

      WGPURenderPassColorAttachment color = {};
      color.view       = backbufferView;
      color.loadOp     = WGPULoadOp_Clear;
      color.storeOp    = WGPUStoreOp_Store;
      color.clearValue = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
      color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

      WGPURenderPassDescriptor passDesc = {};
      passDesc.colorAttachmentCount = 1;
      passDesc.colorAttachments     = &color;
      pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    }

    if (settings.useImGui) {
      ImGui_ImplWGPU_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
    }
  }

  processInput();

  for (auto go : gameObjectsToBeStarted) {
    go->Start();
    gameObjects.insert(go);
  }
  gameObjectsToBeStarted.clear();

  for (auto go : gameObjects) go->Update(deltaTime);

  // Game rendering (OnGui, OnDraw) requires ImGui because Renderer2D is
  // backed by an ImDrawList. If the user opted out of ImGui but registered
  // game objects, surface a one-shot warning so the empty viewport isn't
  // mysterious. See the constraint documented in EngineSettings.h.
  if (!settings.useImGui && !gameObjects.empty()) {
    static bool warned = false;
    if (!warned) {
      SDL_Log("Engine: %zu game object(s) registered but useImGui=false; "
              "their OnGui/OnDraw will not run. Enable useImGui in "
              "EngineSettings to render them.",
              gameObjects.size());
      warned = true;
    }
  }

  if (!settings.headless) {
    if (settings.useImGui) {
      for (auto go : gameObjects)         go->OnGui(window->imGuiContext);
      for (auto go : scriptableObjects)   go->OnGui(window->imGuiContext);

      Renderer2D r(ImGui::GetBackgroundDrawList(),
                   window->size().x, window->size().y);
      for (auto go : gameObjects) go->OnDraw(r);

      ImGui::Render();
    }

    // RmlUi Update must be called every frame to process animations, etc.
    if (settings.useRmlUi && window->rmlContext) {
      window->rmlContext->Update();
    }

    if (pass) {
      // RmlUi Render — uses dynamic uniform offsets to avoid
      // wgpuQueueWriteBuffer ordering issues during the pass.
      if (settings.useRmlUi && window->rmlContext && window->rmlRenderer) {
        // Use physical pixel dimensions for RmlUi so the projection
        // matrix maps context coordinates (also physical) to NDC correctly
        // on HiDPI/Retina displays.
        window->rmlRenderer->PrepareFrame(physW, physH,
                                          physW, physH);
        window->rmlRenderer->BeginRenderPass(pass);
        window->rmlContext->Render();
        window->rmlRenderer->EndRenderPass();
      }
      if (settings.useImGui) {
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
      }
      wgpuRenderPassEncoderEnd(pass);
      wgpuRenderPassEncoderRelease(pass);

      WGPUCommandBufferDescriptor cbDesc = {};
      WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
      wgpuQueueSubmit(window->wgpuQueue, 1, &cb);
      wgpuCommandBufferRelease(cb);
      wgpuCommandEncoderRelease(encoder);

#ifndef __EMSCRIPTEN__
      wgpuSurfacePresent(window->wgpuSurface);
#endif
    }

    if (backbufferView)     wgpuTextureViewRelease(backbufferView);
    if (surfaceTex.texture) wgpuTextureRelease(surfaceTex.texture);
  }

  if (!toDestroy.empty()) {
    for (auto go : toDestroy) {
      gameObjects.erase(go);
      delete go;
    }
    toDestroy.clear();
  }

  if (onTick) onTick();
}

void Engine::Exit() {
  SDL_Log("Exit called");
  done = true;
}

void Engine::processInput() {
  // todo: move this to Input
  static bool up = false, down = false, left = false, right = false;

  arrowInput = Vector2f();

  if (settings.headless) {
    return;
  }

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (settings.useImGui) {
      ImGui_ImplSDL3_ProcessEvent(&event);
    }

    // Feed events to RmlUi
    if (settings.useRmlUi && window->rmlContext) {
      RmlSDL::InputEventHandler(window->rmlContext, window->sdlWindow, event);
    }

    if (event.type == SDL_EVENT_QUIT) done = true;
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event.window.windowID == SDL_GetWindowID(window->sdlWindow))
      done = true;

    switch (event.type) {
      case SDL_EVENT_KEY_DOWN:
        switch (event.key.key) {
          case SDLK_LEFT:  left  = true; break;
          case SDLK_RIGHT: right = true; break;
          case SDLK_UP:    up    = true; break;
          case SDLK_DOWN:  down  = true; break;
          default: break;
        }
        break;
      case SDL_EVENT_KEY_UP:
        switch (event.key.key) {
          case SDLK_LEFT:  left  = false; break;
          case SDLK_RIGHT: right = false; break;
          case SDLK_UP:    up    = false; break;
          case SDLK_DOWN:  down  = false; break;
          default: break;
        }
        break;
    }
  }
  if (up)    arrowInput += Vector2f::up();
  if (down)  arrowInput += Vector2f::down();
  if (left)  arrowInput += Vector2f::left();
  if (right) arrowInput += Vector2f::right();
}

Vector2f Engine::getInputArrow() const { return arrowInput; }

//// todo: optimize this
// template <class T> std::unordered_set<T> Engine::FindObjectsOfType() {
//   std::unordered_set<T> ret;
//   for (GameObject* go : gameObjects)
//     if (T elem = dynamic_cast<T>(&go))  // todo: check this
//       ret.insert(elem);
//
//   return ret;
// }

// todo: optimize this
void Engine::Destroy(GameObject* go) { toDestroy.push_back(go); }
