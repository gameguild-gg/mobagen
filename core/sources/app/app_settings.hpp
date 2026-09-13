#pragma once
// ============================================================================
// AppSettings — startup configuration consumed by the SDL3 callback host.
// ============================================================================
// Apps mutate these from AppCallbacks::on_init, which the host runs at the top
// of SDL_AppInit BEFORE any SDL_Init/window/GPU object exists — that is where
// the render mode, title and window size are chosen. AppSettings::parse maps
// the host's own --mobagen-* CLI flags onto the render mode from there.
//
// HeadlessNull requests a Dawn null-backend device and renders into an
// offscreen texture (on_draw runs, nothing is presented). When the null
// backend is unavailable — web builds, or a native build without it — the
// host logs and degrades to HeadlessNone. HeadlessNone never creates any GPU
// object; on_draw is not called.
#include <webgpu/webgpu.h>

namespace app {

  struct AppSettings {
    enum class RenderMode {
      Windowed,      ///< Window + WebGPU surface (the normal app path).
      HeadlessNull,  ///< Offscreen Dawn null device; no window, no present.
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

    // Scan argv (from argv[1]) for the host's render-mode flags:
    //   --mobagen-headless  -> RenderMode::HeadlessNone
    //   --mobagen-null-gpu  -> RenderMode::HeadlessNull
    // Unknown arguments are IGNORED (apps layer their own parsing on top);
    // when both flags appear, the last one wins. Returns true when a flag was
    // recognized. Call from on_init, e.g. AppSettings::parse(argc, argv, app.settings).
    static bool parse(int argc, char** argv, AppSettings& out_settings);
  };

}  // namespace app
