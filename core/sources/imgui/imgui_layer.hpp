#pragma once
// ============================================================================
// ImGuiLayer — GuiLayerView implementation wrapping Dear ImGui's SDL3 platform
// backend + WebGPU renderer backend for the core app host.
// ============================================================================
// Attach from AppCallbacks::on_init via app.attach_gui(layer); the host then
// calls init/process_event/new_frame/render/on_surface_resized/shutdown at the
// contract points (see app.hpp). Boot code and frame order are the proven
// family-A sequence (apps/chess/main.cpp:183-208, upstream example_sdl3_wgpu).
//
// Headless awareness: the SDL3 platform backend requires a window — headless
// modes skip it while the ImGui context still exists, so on_draw GUI code has
// a valid context (the host only opens a GUI frame when a device exists, i.e.
// windowed or headless-null). HeadlessNone has no device at all: init() fails
// cleanly and the app exits through the normal host failure path.
#include "app/app.hpp"

namespace app {

  class ImGuiLayer : public GuiLayerView {
  public:
    // Creates the ImGui context (nav flags, DPI-scaled dark style), inits the
    // SDL3 platform backend when the host has a window and the WebGPU backend
    // when it has a device. Returns the WGPU backend result; false (logged)
    // when there is no device to render into.
    bool init(App& app) override;
    void process_event(const SDL_Event& event) override;
    // Platform NewFrame first, then WGPU NewFrame, then ImGui::NewFrame
    // (upstream example_sdl3_wgpu order; chess main.cpp:241-243).
    void new_frame() override;
    void render(WGPURenderPassEncoder pass) override;
    void on_surface_resized() override;
    // Reverse init order; tolerant of never-initialized state.
    void shutdown() override;

  private:
    bool platform_backend_ready_ = false;  // SDL3 backend inited (needs a window)
    bool wgpu_ready_ = false;              // WebGPU backend inited (needs a device)
  };

}  // namespace app
