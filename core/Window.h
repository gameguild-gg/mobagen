#pragma once

#include "math/Point2D.h"
#include "math/Vector2.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <webgpu/webgpu.h>
#include <imgui.h>
#include <string>

namespace Rml {
class Context;
class SystemInterface;
}
class RmlUiWgpuRenderer;

// A thin owner of the OS window + WebGPU surface/device used by the engine.
// No SDL_Renderer anymore — all drawing goes through WebGPU + ImGui.
class Window {
public:
  explicit Window(std::string title);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Current window size in pixels (drawable size, HiDPI-aware).
  Point2D size() const { return windowSize; }

  // Per-frame housekeeping: detect resize, reconfigure WebGPU surface,
  // adapt ImGui font scale. Called by Engine::Tick.
  void Update();

  // Initialize RmlUi (HTML/CSS UI). Call once after construction.
  // Creates the SystemInterface, RenderInterface, context, and loads fonts.
  void InitRmlUi();

  // --- public for the Engine; demos shouldn't touch these directly ---
  SDL_Window*       sdlWindow     = nullptr;
  ImGuiContext*     imGuiContext  = nullptr;

  WGPUInstance      wgpuInstance  = nullptr;
  WGPUAdapter       wgpuAdapter   = nullptr;
  WGPUDevice        wgpuDevice    = nullptr;
  WGPUQueue         wgpuQueue     = nullptr;
  WGPUSurface       wgpuSurface   = nullptr;
  WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;

  // Backed by SDL_Metal_CreateView on macOS; nullptr elsewhere.
  SDL_MetalView     metalView     = nullptr;

  // RmlUi. RmlUi holds non-owning pointers to the system and render
  // interfaces, so we keep them alive and free them after Rml::Shutdown().
  Rml::Context*           rmlContext          = nullptr;
  RmlUiWgpuRenderer*      rmlRenderer         = nullptr;
  void*                   rmlSystemInterface  = nullptr;  // SystemInterface_SDL*

private:
  void createSurface();
  void configureSurface(int widthPx, int heightPx);
  void initDeviceAndQueue();

  Point2D windowSize{0, 0};
};
