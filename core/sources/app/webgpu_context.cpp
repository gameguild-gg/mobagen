// ============================================================================
// WebGPUContext — implementation. See webgpu_context.hpp for the contract.
// ============================================================================
// The adapter/device request strategies are copied from the proven app code:
//  - native: apps/flocking/main.cpp (TimedWaitAny + WaitAnyOnly + WaitAny)
//  - web:    apps/hideandseeksquared/main.cpp (AllowProcessEvents + pump loop;
//            apps/dicom_viewer documents that the web requires pumping)
#include "app/webgpu_context.hpp"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#ifndef __EMSCRIPTEN__
#  include <webgpu/webgpu_cpp.h>
#endif

#if defined(SDL_PLATFORM_APPLE)
#  include <SDL3/SDL_metal.h>
#endif

#include <cstdint>

namespace app {

#ifndef __EMSCRIPTEN__
  namespace {

    // Synchronous adapter request (flocking RequestAdapter). The backend/power
    // selection maps the C enum members onto their wgpu:: counterparts 1:1.
    wgpu::Adapter request_adapter(wgpu::Instance& instance, const ContextDesc& desc) {
      wgpu::Adapter acquired;
      wgpu::RequestAdapterOptions opts;
      opts.powerPreference = static_cast<wgpu::PowerPreference>(desc.power_preference);
      if (desc.backend_type != WGPUBackendType_Undefined) {
        opts.backendType = static_cast<wgpu::BackendType>(desc.backend_type);
      }
      auto cb = [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView msg) {
        if (status != wgpu::RequestAdapterStatus::Success) {
          SDL_Log("RequestAdapter failed: %s", msg.data);
          return;
        }
        acquired = std::move(adapter);
      };
      wgpu::Future f{instance.RequestAdapter(&opts, wgpu::CallbackMode::WaitAnyOnly, cb)};
      instance.WaitAny(f, UINT64_MAX);
      return acquired;
    }

    // Synchronous device request with device-lost / uncaptured-error logging
    // (flocking RequestDevice, including its SDL_Log wording).
    wgpu::Device request_device(wgpu::Instance& instance, wgpu::Adapter& adapter, std::atomic<ContextState>* state) {
      wgpu::DeviceDescriptor desc;
      desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                 [state](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView msg) {
                                   mark_context_lost(*state);
                                   SDL_Log("WebGPU device lost (%d): %s", static_cast<int>(reason), msg.data);
                                 });
      desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView msg) {
        SDL_Log("WebGPU error (%d): %s", static_cast<int>(type), msg.data);
      });
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
      return acquired;
    }

  }  // namespace
#else
  namespace {

    // Web request state + callbacks (hideandseeksquared AdapterReq/DeviceReq).
    struct AdapterReq {
      WGPUAdapter adapter = nullptr;
      bool done = false;
    };
    void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void* ud1, void*) {
      auto* r = static_cast<AdapterReq*>(ud1);
      if (status == WGPURequestAdapterStatus_Success) {
        r->adapter = adapter;
      } else {
        SDL_Log("RequestAdapter failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
      }
      r->done = true;
    }

    struct DeviceReq {
      WGPUDevice device = nullptr;
      bool done = false;
    };
    void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void* ud1, void*) {
      auto* r = static_cast<DeviceReq*>(ud1);
      if (status == WGPURequestDeviceStatus_Success) {
        r->device = device;
      } else {
        SDL_Log("RequestDevice failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
      }
      r->done = true;
    }

    void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView msg, void* ud1, void*) {
      mark_context_lost(*static_cast<std::atomic<ContextState>*>(ud1));
      SDL_Log("WebGPU device lost (%d): %.*s", (int)reason, (int)msg.length, msg.data ? msg.data : "");
    }

    void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
      SDL_Log("WebGPU error (%d): %.*s", (int)type, (int)msg.length, msg.data ? msg.data : "");
    }

    // Pump instance events until the request callback lands. Mandatory on web:
    // emdawnwebgpu resolves adapter/device futures from JS promises, but
    // AllowProcessEvents callbacks are only delivered while events are
    // processed (see apps/dicom_viewer/sources/main.cpp). SDL_Delay yields to
    // the browser event loop under asyncify.
    bool pump_until(WGPUInstance inst, bool& flag, const char* operation, Uint64 timeout_ms = 10000) {
      const Uint64 start = SDL_GetTicks();
      while (!flag) {
        wgpuInstanceProcessEvents(inst);
        if (SDL_GetTicks() - start > timeout_ms) {
          SDL_Log("%s timed out after %llu ms", operation, static_cast<unsigned long long>(timeout_ms));
          return false;
        }
        SDL_Delay(1);
      }
      return true;
    }

  }  // namespace
#endif  // __EMSCRIPTEN__

  // ---------------------------------------------------------------------------
  // Surface creation — hand-rolled WGPUSurfaceSource chains.
  // ---------------------------------------------------------------------------
  // WGPUSurfaceDescriptor holds its chain by POINTER: the per-platform source
  // descriptor must outlive the wgpuInstanceCreateSurface call, so every
  // descriptor lives at the platform-branch scope with the create at its end
  // (branch-inner locals leave nextInChain dangling — dawn then validates
  // garbage: "Wayland surface is nullptr" / "Invalid X Window").
  bool WebGPUContext::create_surface(WGPUInstance instance, const ContextDesc& context) {
    WGPUSurfaceDescriptor desc = {};
#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_desc = {};
    canvas_desc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas_desc.selector = {"#canvas", WGPU_STRLEN};
    desc.nextInChain = &canvas_desc.chain;
    surface_ = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_WIN32)
    WGPUSurfaceSourceWindowsHWND hwnd_desc = {};
    hwnd_desc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    if (context.native_surface != nullptr) {
      if (context.native_surface->kind != NativeSurfaceKind::Win32) return false;
      hwnd_desc.hinstance = context.native_surface->display;
      hwnd_desc.hwnd = context.native_surface->window;
    } else {
      const SDL_PropertiesID props = SDL_GetWindowProperties(context.window);
      hwnd_desc.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
      hwnd_desc.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    }
    desc.nextInChain = &hwnd_desc.chain;
    surface_ = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_APPLE)
    WGPUSurfaceSourceMetalLayer metal_desc = {};
    metal_desc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    if (context.native_surface != nullptr) {
      if (context.native_surface->kind != NativeSurfaceKind::MetalLayer) return false;
      metal_desc.layer = context.native_surface->window;
    } else {
      metal_view_ = SDL_Metal_CreateView(context.window);
      metal_desc.layer = SDL_Metal_GetLayer(metal_view_);
    }
    desc.nextInChain = &metal_desc.chain;
    surface_ = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_LINUX)
    WGPUSurfaceSourceWaylandSurface wayland_desc = {};
    WGPUSurfaceSourceXlibWindow xlib_desc = {};
    const auto* native = context.native_surface;
    const bool wayland = native != nullptr
                             ? native->kind == NativeSurfaceKind::Wayland
                             : SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0;
    if (wayland) {
      // Wayland sessions: an X11-only chain crashes with "Unsupported sType".
      wayland_desc.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
      if (native != nullptr) {
        wayland_desc.display = native->display;
        wayland_desc.surface = native->window;
      } else {
        const SDL_PropertiesID props = SDL_GetWindowProperties(context.window);
        wayland_desc.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        wayland_desc.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
      }
      desc.nextInChain = &wayland_desc.chain;
    } else {
      if (native != nullptr && native->kind != NativeSurfaceKind::Xlib) return false;
      xlib_desc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
      if (native != nullptr) {
        xlib_desc.display = native->display;
        xlib_desc.window = native->window_id;
      } else {
        const SDL_PropertiesID props = SDL_GetWindowProperties(context.window);
        xlib_desc.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        xlib_desc.window = static_cast<std::uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
      }
      desc.nextInChain = &xlib_desc.chain;
    }
    surface_ = wgpuInstanceCreateSurface(instance, &desc);
#else
    SDL_Log("Unsupported platform for WebGPU surface creation");
    return false;
#endif
    if (!surface_) {
      SDL_Log("Failed to create WebGPU surface");
      return false;
    }
    return true;
  }

  // ---------------------------------------------------------------------------
  // init / shutdown
  // ---------------------------------------------------------------------------
  WebGPUContext::~WebGPUContext() { shutdown(); }

  bool WebGPUContext::init(const ContextDesc& desc) {
    if (desc.want_surface && desc.window == nullptr && desc.native_surface == nullptr) {
      SDL_Log("WebGPUContext::init: want_surface requires a window or native surface");
      return false;
    }
    if (desc.want_surface && desc.native_surface != nullptr
        && (desc.native_surface->kind == NativeSurfaceKind::None
            || desc.native_surface->width <= 0 || desc.native_surface->height <= 0)) {
      SDL_Log("WebGPUContext::init: native surface is invalid");
      return false;
    }
    ContextState expected = ContextState::Uninitialized;
    if (!state_.compare_exchange_strong(expected, ContextState::Initializing, std::memory_order_acq_rel, std::memory_order_acquire)) {
      SDL_Log("WebGPUContext::init: context is already initialized");
      return false;
    }

#ifndef __EMSCRIPTEN__
    // Native path (flocking InitWGPU): TimedWaitAny instance, synchronous
    // adapter/device requests, surface from the SDL window.
    wgpu::InstanceDescriptor inst_desc = {};
    static constexpr wgpu::InstanceFeatureName kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    inst_desc.requiredFeatureCount = 1;
    inst_desc.requiredFeatures = &kTimedWaitAny;
    wgpu::Instance instance = wgpu::CreateInstance(&inst_desc);
    if (!instance) {
      SDL_Log("Failed to create WebGPU instance");
      shutdown();
      return false;
    }

    wgpu::Adapter adapter = request_adapter(instance, desc);
    if (!adapter) {
      shutdown();
      return false;  // instance + adapter clean themselves up
    }

    device_ = request_device(instance, adapter, &state_).MoveToCHandle();
    if (!device_) {
      shutdown();  // releases device_ if partially set; instance/adapter still own themselves
      return false;
    }

    if (desc.want_surface && !create_surface(instance.Get(), desc)) {
      shutdown();
      return false;
    }

    adapter_ = adapter.MoveToCHandle();
    instance_ = instance.MoveToCHandle();
#else
    // Web path (hideandseeksquared init + initDeviceAndQueue): instance, canvas
    // surface, then adapter/device through pumpable AllowProcessEvents requests.
    instance_ = wgpuCreateInstance(nullptr);
    if (!instance_) {
      SDL_Log("Failed to create WebGPU instance");
      shutdown();
      return false;
    }

    if (desc.want_surface && !create_surface(instance_, desc)) {
      shutdown();
      return false;
    }

    AdapterReq a_req;
    WGPURequestAdapterOptions a_opts = {};
    a_opts.compatibleSurface = surface_;
    a_opts.powerPreference = desc.power_preference;
    if (desc.backend_type != WGPUBackendType_Undefined) {
      a_opts.backendType = desc.backend_type;
    }
    WGPURequestAdapterCallbackInfo a_cb = {};
    a_cb.mode = WGPUCallbackMode_AllowProcessEvents;
    a_cb.callback = on_adapter;
    a_cb.userdata1 = &a_req;
    wgpuInstanceRequestAdapter(instance_, &a_opts, a_cb);
    if (!pump_until(instance_, a_req.done, "requestAdapter")) {
      shutdown();
      return false;
    }
    if (!a_req.adapter) {
      SDL_Log("No WebGPU adapter available");
      shutdown();
      return false;
    }
    adapter_ = a_req.adapter;

    DeviceReq d_req;
    WGPUDeviceDescriptor d_desc = {};
    d_desc.label = {"mobagen device", WGPU_STRLEN};
    d_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    d_desc.deviceLostCallbackInfo.callback = on_device_lost;
    d_desc.deviceLostCallbackInfo.userdata1 = &state_;
    d_desc.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
    WGPURequestDeviceCallbackInfo d_cb = {};
    d_cb.mode = WGPUCallbackMode_AllowProcessEvents;
    d_cb.callback = on_device;
    d_cb.userdata1 = &d_req;
    wgpuAdapterRequestDevice(adapter_, &d_desc, d_cb);
    if (!pump_until(instance_, d_req.done, "requestDevice")) {
      shutdown();
      return false;
    }
    if (!d_req.device) {
      SDL_Log("WebGPU device request failed");
      shutdown();
      return false;
    }
    device_ = d_req.device;
#endif  // __EMSCRIPTEN__

    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
      SDL_Log("Failed to get the WebGPU queue");
      shutdown();
      return false;
    }

    if (surface_ != nullptr) {
      WGPUSurfaceCapabilities caps = {};
      wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
      surface_format_ = (caps.formatCount > 0 && caps.formats) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
      wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    surface_cfg_ = {};
    surface_cfg_.device = device_;
    surface_cfg_.format = surface_format_;
    surface_cfg_.usage = WGPUTextureUsage_RenderAttachment;
    surface_cfg_.alphaMode = WGPUCompositeAlphaMode_Auto;
    surface_cfg_.presentMode = WGPUPresentMode_Fifo;

    expected = ContextState::Initializing;
    if (!state_.compare_exchange_strong(expected, ContextState::Operational, std::memory_order_acq_rel, std::memory_order_acquire)) {
      SDL_Log("WebGPU context became unavailable during initialization");
      shutdown();
      return false;
    }

    if (surface_ != nullptr) {
      int w = 0, h = 0;
      if (desc.native_surface != nullptr) {
        w = desc.native_surface->width;
        h = desc.native_surface->height;
      } else {
        SDL_GetWindowSizeInPixels(desc.window, &w, &h);
      }
      if (!configure_surface(w, h)) {
        SDL_Log("Failed to configure WebGPU surface");
        shutdown();
        return false;
      }
    }

    SDL_Log("WebGPU context ready (surfaceFormat=%d)", static_cast<int>(surface_format_));
    return true;
  }

  void WebGPUContext::shutdown() {
    // Publish shutdown before releasing callback-owned objects. A late device-
    // lost callback cannot change a completed shutdown back to Lost.
    state_.store(ContextState::Uninitialized, std::memory_order_release);
    if (surface_ != nullptr) {
      if (surface_configured_) wgpuSurfaceUnconfigure(surface_);
      wgpuSurfaceRelease(surface_);
      surface_ = nullptr;
      surface_configured_ = false;
    }
    if (queue_ != nullptr) {
      wgpuQueueRelease(queue_);
      queue_ = nullptr;
    }
    if (device_ != nullptr) {
      wgpuDeviceRelease(device_);
      device_ = nullptr;
    }
    if (adapter_ != nullptr) {
      wgpuAdapterRelease(adapter_);
      adapter_ = nullptr;
    }
    if (instance_ != nullptr) {
      wgpuInstanceRelease(instance_);
      instance_ = nullptr;
    }
#if defined(SDL_PLATFORM_APPLE)
    if (metal_view_ != nullptr) {
      SDL_Metal_DestroyView(metal_view_);
      metal_view_ = nullptr;
    }
#endif
    surface_format_ = WGPUTextureFormat_Undefined;
    surface_cfg_ = {};
  }

  // ---------------------------------------------------------------------------
  // Frame surface flow
  // ---------------------------------------------------------------------------
  bool WebGPUContext::configure_surface(int width, int height) {
    if (!operational() || surface_ == nullptr || device_ == nullptr) return false;
    if (width <= 0 || height <= 0) return false;
    surface_cfg_.width = static_cast<std::uint32_t>(width);
    surface_cfg_.height = static_cast<std::uint32_t>(height);
    wgpuSurfaceConfigure(surface_, &surface_cfg_);
    surface_configured_ = true;
    return operational();
  }

  WGPUSurfaceTexture WebGPUContext::acquire() {
    WGPUSurfaceTexture surface_texture = {};
    if (!operational() || surface_ == nullptr || !surface_configured_) return surface_texture;
    wgpuSurfaceGetCurrentTexture(surface_, &surface_texture);
    if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Lost || surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Error) {
      mark_context_lost(state_);
    }
    return surface_texture;
  }

  bool WebGPUContext::present() {
    if (!operational() || surface_ == nullptr || !surface_configured_) return false;
    wgpuSurfacePresent(surface_);
    return operational();
  }

  bool WebGPUContext::tick() {
    if (!operational()) return false;
#ifdef __EMSCRIPTEN__
    if (instance_ != nullptr) wgpuInstanceProcessEvents(instance_);
#else
    if (device_ != nullptr) wgpuDeviceTick(device_);
#endif
    return operational();
  }

}  // namespace app
