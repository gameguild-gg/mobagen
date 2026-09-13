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

#include <atomic>
#include <cstdint>

struct SDL_Window;

namespace app {

  enum class ContextState : std::uint8_t { Uninitialized, Initializing, Operational, Lost };

  enum class SurfaceFrameAction : std::uint8_t { Render, RenderThenReconfigure, Retry, Reconfigure, Fail };

  constexpr SurfaceFrameAction surface_frame_action(WGPUSurfaceGetCurrentTextureStatus status, bool has_texture) noexcept {
    switch (status) {
      case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
        return has_texture ? SurfaceFrameAction::Render : SurfaceFrameAction::Fail;
      case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        return has_texture ? SurfaceFrameAction::RenderThenReconfigure : SurfaceFrameAction::Fail;
      case WGPUSurfaceGetCurrentTextureStatus_Timeout:
        return SurfaceFrameAction::Retry;
      case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        return SurfaceFrameAction::Reconfigure;
      case WGPUSurfaceGetCurrentTextureStatus_Lost:
      case WGPUSurfaceGetCurrentTextureStatus_Error:
      default:
        return SurfaceFrameAction::Fail;
    }
  }

  inline void mark_context_lost(std::atomic<ContextState>& state) noexcept {
    ContextState current = state.load(std::memory_order_acquire);
    while (current != ContextState::Uninitialized && current != ContextState::Lost
           && !state.compare_exchange_weak(current, ContextState::Lost, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
  }

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
    ContextState state() const noexcept { return state_.load(std::memory_order_acquire); }
    bool operational() const noexcept { return state() == ContextState::Operational; }
    bool lost() const noexcept { return state() == ContextState::Lost; }

    bool configure_surface(int width, int height);
    WGPUSurfaceTexture acquire();
    bool present();
    bool tick();

  private:
    bool create_surface(WGPUInstance instance, SDL_Window* window);

    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
    WGPUSurfaceConfiguration surface_cfg_ = {};
    std::atomic<ContextState> state_{ContextState::Uninitialized};
    // wgpuSurfaceUnconfigure on a never-configured surface (or one whose
    // configure failed) trips a dawn DAWN_CHECK assert — only unconfigure a
    // surface we actually configured.
    bool surface_configured_ = false;
#if defined(__APPLE__)
    void* metal_view_ = nullptr;  // SDL_MetalView (a void*), kept opaque so this header needs no SDL include
#endif
  };

}  // namespace app
