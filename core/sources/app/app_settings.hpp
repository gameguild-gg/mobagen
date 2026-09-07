#pragma once
// ============================================================================
// AppSettings — startup configuration consumed by the SDL3 callback host.
// ============================================================================
// Apps mutate these from AppCallbacks::on_init, which the host runs at the top
// of SDL_AppInit BEFORE any SDL_Init/window/GPU object exists — that is where
// the render mode, title and window size are chosen.
//
// RenderMode::HeadlessNull is DECLARED here, but its offscreen device path
// lands with the headless-mode work (core-app-host todo 3). Until then both
// headless modes take the "no window, no GPU" path: a pure logic loop with no
// WebGPU objects at all.
#include <webgpu/webgpu.h>

namespace app {

  struct AppSettings {
    enum class RenderMode {
      Windowed,      ///< Window + WebGPU surface (the normal app path).
      HeadlessNull,  ///< Offscreen Dawn null device (todo 3); no window.
      HeadlessNone,  ///< Pure logic loop: no window, no GPU objects at all.
    };

    const char* title = "MoBaGEn App";
    int width = 1280;  // logical size; windowed init scales it by display content scale
    int height = 800;
    bool resizable = true;
    bool high_pixel_density = false;
    float clear_color[4] = {0.10f, 0.10f, 0.10f, 1.00f};
    WGPUPowerPreference power_preference = WGPUPowerPreference_HighPerformance;
    RenderMode render_mode = RenderMode::Windowed;
  };

}  // namespace app
