// ============================================================================
// ImGuiLayer — Dear ImGui (SDL3 + WebGPU backends) on the core app host.
// ============================================================================
// Boot/frame/shutdown copied from the proven family-A mains
// (apps/chess/main.cpp:183-208): DPI-scaled dark style with nav flags,
// SDL3 platform backend (window events) + WebGPU renderer backend (device,
// InitInfo struct — v1.92.8-docking), each frame platform-NewFrame ->
// WGPU-NewFrame -> NewFrame (upstream example_sdl3_wgpu order), then
// RenderDrawData inside the host's open render pass. The two backends are
// guarded by independent flags so headless modes (no window) still get a
// usable context through the renderer backend alone.
#include "imgui/imgui_layer.hpp"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_wgpu.h>
#include <imgui.h>

namespace app {

  bool ImGuiLayer::init(App& app) {
    // --- context + style (chess main.cpp:183-192) ---------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    // DPI-scale the style like the windowed apps do (chess main.cpp:190-192);
    // without a window there is no meaningful display scale — keep 1.0.
    const float scale = app.window != nullptr ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) : 1.0f;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    // --- platform backend: needs a window (chess main.cpp:194) --------------
    // Headless modes skip it entirely; ImGui_ImplSDL3_* calls below are all
    // guarded by platform_backend_ready_.
    if (app.window != nullptr) platform_backend_ready_ = ImGui_ImplSDL3_InitForOther(app.window);

    // --- renderer backend: needs a device (chess main.cpp:196-201) ----------
    if (app.device() == nullptr) {
      // HeadlessNone: no GPU objects exist and the host never opens a GUI
      // frame there — nothing to init against, fail cleanly.
      SDL_Log("ImGuiLayer: no WebGPU device, GUI unavailable");
      return false;
    }
    ImGui_ImplWGPU_InitInfo wgpu_init = {};
    wgpu_init.Device = app.device();
    wgpu_init.NumFramesInFlight = 3;
    // Headless-null has no surface, so gpu.surface_format() reports Undefined
    // there — the host's offscreen render target is BGRA8Unorm (app.cpp).
    wgpu_init.RenderTargetFormat = app.gpu.surface_format() != WGPUTextureFormat_Undefined
                                       ? app.gpu.surface_format()
                                       : WGPUTextureFormat_BGRA8Unorm;
    wgpu_init.DepthStencilFormat = WGPUTextureFormat_Undefined;
    wgpu_ready_ = ImGui_ImplWGPU_Init(&wgpu_init);
    if (!wgpu_ready_) SDL_Log("ImGui_ImplWGPU_Init failed");
    return wgpu_ready_;
  }

  void ImGuiLayer::process_event(const SDL_Event& event) {
    if (platform_backend_ready_) ImGui_ImplSDL3_ProcessEvent(&event);
  }

  void ImGuiLayer::new_frame() {
    if (platform_backend_ready_) ImGui_ImplSDL3_NewFrame();
    if (wgpu_ready_) ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();
  }

  void ImGuiLayer::render(WGPURenderPassEncoder pass) {
    ImGui::Render();
    if (wgpu_ready_) ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
  }

  void ImGuiLayer::on_surface_resized() {
    // Intentionally empty: the canonical resize path is surface reconfigure
    // only (core-app-host plan, todo 2 Metis decision) — the ImGui WebGPU
    // backend needs no Invalidate/CreateDeviceObjects dance on v1.92.8.
  }

  void ImGuiLayer::shutdown() {
    // Reverse init order. Tolerates never-initialized state: the flags stay
    // false and DestroyContext is a no-op without a current context.
    if (wgpu_ready_) ImGui_ImplWGPU_Shutdown();
    if (platform_backend_ready_) ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    wgpu_ready_ = false;
    platform_backend_ready_ = false;
  }

}  // namespace app
