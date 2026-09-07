#pragma once
// ============================================================================
// RmlUiLayer — GuiLayerView implementation rendering RmlUi documents through
// ImGui's WebGPU pipeline (the ImGui bridge).
// ============================================================================
// Composition over re-implementation: an internal ImGuiLayer (core_imgui)
// owns the ImGui SDL3 platform backend + WebGPU renderer backend, and
// RmlImGuiRenderer (in the .cpp) bridges RmlUi layout geometry into ImGui's
// background draw list — RmlUi rides the same render pass as the rest of the
// frame, no separate RmlUi-WebGPU renderer (native path is future work,
// core-app-host plan guardrail).
//
// Attach from AppCallbacks::on_init via app.attach_gui(layer); document
// content is app data — apps load their .rml through the Rml::Context
// directly via context() (also the hook for diagnostics and the RmlUi
// debugger). Boot/font setup moved verbatim from apps/rmluidemo/main.cpp.
//
// Headless awareness: mirrors ImGuiLayer's contract — init() fails cleanly
// (logged, via ImGuiLayer) when the host has no WebGPU device, and the
// RmlSDL input handler needs a window, so event forwarding is skipped in
// headless modes while layout/rendering still run against the null device.
#include "app/app.hpp"
#include "imgui/imgui_layer.hpp"

#include <memory>

namespace Rml {
  class Context;
}
class SystemInterface_SDL;

namespace app {

  // Defined in the .cpp (RmlUi-free public header).
  class RmlImGuiRenderer;

  class RmlUiLayer : public GuiLayerView {
  public:
    // Out-of-line ctor/dtor: members hold incomplete types (RmlUi-free
    // public header).
    RmlUiLayer();
    ~RmlUiLayer() override;

    // ImGui frame first (the bridge needs it), then the RmlUi system/render
    // interfaces, the embedded fonts, the "main" context and the debugger.
    // Returns false (logged) when ImGuiLayer::init reports no device.
    bool init(App& app) override;
    // ImGui backend events + RmlSDL::InputEventHandler (window-dependent part
    // skipped headless).
    void process_event(const SDL_Event& event) override;
    // Opens the ImGui frame (imgui_layer_), then updates the RmlUi context
    // and emits its geometry into ImGui's background draw list.
    void new_frame() override;
    // The bridge already emitted everything — ImGui::Render + RenderDrawData.
    void render(WGPURenderPassEncoder pass) override;
    // context->SetDimensions(app.width(), app.height()).
    void on_surface_resized() override;
    // Rml::Shutdown then ImGui teardown; tolerant of never-initialized state.
    void shutdown() override;

    // For diagnostics overlays, custom event handlers and the debugger.
    Rml::Context* context() const { return context_; }

  private:
    ImGuiLayer imgui_layer_;
    App* app_ = nullptr;                        // for window/events + resize
    Rml::Context* context_ = nullptr;
    std::unique_ptr<SystemInterface_SDL> system_interface_;
    std::unique_ptr<RmlImGuiRenderer> renderer_;
    bool rml_initialised_ = false;  // gates Rml::Shutdown (tolerates failed init)
  };

}  // namespace app
