#pragma once
// ============================================================================
// WebGPUContext — per-app WebGPU instance/adapter/device/queue (+ surface).
// ============================================================================
// Extracted from the per-app boilerplate: the native path follows the family-A
// apps (TimedWaitAny instance feature + WaitAnyOnly callbacks for synchronous
// adapter/device requests), the web path follows the family-B apps
// (AllowProcessEvents callbacks + an event pump, because emdawnwebgpu only
// delivers callbacks while instance events are processed).
//
// Surface creation is hand-rolled per platform (cocoa MetalLayer, wayland,
// x11, win32 HWND; "#canvas" selector under Emscripten) — no GUI-backend
// helper is involved. On Linux both wayland and x11 chains are supported; the
// choice is made at runtime from the active SDL video driver.
//
// This class never creates a window: the caller supplies one via ContextDesc
// when it wants a surface, and owns the main loop — tick()/present() drive the
// wgpu side of a frame only.

#include <webgpu/webgpu.h>

struct SDL_Window;

namespace app {

  struct ContextDesc {
    WGPUPowerPreference power_preference = WGPUPowerPreference_HighPerformance;
    WGPUBackendType backend_type = WGPUBackendType_Undefined;  // Undefined = let the API pick
    bool want_surface = true;
    SDL_Window* window = nullptr;  // required when want_surface
  };

  class WebGPUContext {
  public:
    WebGPUContext() = default;
    ~WebGPUContext();

    WebGPUContext(const WebGPUContext&) = delete;
    WebGPUContext& operator=(const WebGPUContext&) = delete;
    WebGPUContext(WebGPUContext&&) = delete;
    WebGPUContext& operator=(WebGPUContext&&) = delete;

    bool init(const ContextDesc& desc);
    void shutdown();  // idempotent; also invoked by the destructor

    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }
    WGPUSurface surface() const { return surface_; }  // nullptr when headless
    WGPUTextureFormat surface_format() const { return surface_format_; }

    void configure_surface(int width, int height);
    WGPUSurfaceTexture acquire();
    void present();
    void tick();

  private:
    bool create_surface(WGPUInstance instance, SDL_Window* window);

    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
    WGPUSurfaceConfiguration surface_cfg_ = {};
    bool initialized_ = false;
    // wgpuSurfaceUnconfigure on a never-configured surface (or one whose
    // configure failed) trips a dawn DAWN_CHECK assert — only unconfigure a
    // surface we actually configured.
    bool surface_configured_ = false;
#if defined(__APPLE__)
    void* metal_view_ = nullptr;  // SDL_MetalView (a void*), kept opaque so this header needs no SDL include
#endif
  };

}  // namespace app
