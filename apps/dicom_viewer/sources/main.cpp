// Prevent SDL from redefining main() to SDL_main()
// We want to control the entry point ourselves
#define SDL_MAIN_HANDLED

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  include <emscripten/html5.h>
#endif

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "camera/camera.hpp"

// ============================================================================
// SINGLE-RENDERER BUILD MODEL
// ============================================================================
//
// LEARNING: A browser <canvas> can hold exactly ONE context for its lifetime.
// Once getContext('webgl2') is called, getContext('webgpu') returns null (and
// vice versa). So a runtime "switch renderer on the same canvas" is impossible.
//
// Instead, each renderer is its own BUILD, selected at compile time:
//   - USE_WEBGPU OFF (default) -> WebGL2 build  (the learning rung)
//   - USE_WEBGPU ON            -> WebGPU build  (the destination: compute shaders)
//
// Both builds share the same camera + input code below. The renderer-specific
// code is guarded by #ifdef USE_WEBGPU.
//
// CMake produces them in separate output dirs:
//   build/wasm-webgl/bin/dicom_renderer.html
//   build/wasm-webgpu/bin/dicom_renderer.html
// ============================================================================

// ============================================================================
// CAMERA & INPUT (shared by both renderer builds)
// ============================================================================
static engine::Camera g_camera(engine::CameraMode::ORBIT);

// Mouse-look is GATED: the camera only rotates while a mouse button is held.
// This lets the user move the cursor to click UI without spinning the camera.
static bool g_mouse_look_active = false;
enum class MouseDragAction { None, Rotate, Pan };
static MouseDragAction g_mouse_drag_action = MouseDragAction::None;

// Current canvas drawing-buffer size, pushed from the shell via on_canvas_resize
// (the authoritative size; SDL's window size is stale on the web). 0 = not set.
static int g_canvas_w = 0;
static int g_canvas_h = 0;

// Real frame delta time, measured from SDL's high-resolution counter.
static float measure_delta_seconds() {
  static uint64_t last = SDL_GetPerformanceCounter();
  const uint64_t now = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  float dt = static_cast<float>(static_cast<double>(now - last) / freq);
  last = now;
  // Clamp to avoid huge jumps after a stall / tab switch.
  if (dt > 0.1f) dt = 0.1f;
  return dt;
}

static void get_drawable_size(SDL_Window* window, int& width, int& height) {
  width = 0;
  height = 0;
#ifdef __EMSCRIPTEN__
  // On the web, the HTML canvas drawing buffer is the authoritative viewport.
  // SDL's window size can lag behind after CSS/device-pixel-ratio changes.
  if (g_canvas_w > 0 && g_canvas_h > 0) {
    width = g_canvas_w;
    height = g_canvas_h;
    return;
  }
#endif
  if (window) {
    SDL_GetWindowSizeInPixels(window, &width, &height);
  }
  if (width <= 0 || height <= 0) {
    width = 800;
    height = 600;
  }
}

static void toggle_pointer_lock() {
#ifdef __EMSCRIPTEN__
  EmscriptenPointerlockChangeEvent status;
  if (emscripten_get_pointerlock_status(&status) == EMSCRIPTEN_RESULT_SUCCESS && status.isActive) {
    emscripten_exit_pointerlock();
    printf("Pointer lock: off\n");
  } else {
    emscripten_request_pointerlock("#canvas", EM_TRUE);
    printf("Pointer lock: requested\n");
  }
#else
  printf("Pointer lock is browser-only; native SDL already receives relative motion while dragging.\n");
#endif
}

static bool is_descend_key(SDL_Keycode key) { return key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL; }

static void handle_camera_key_down(SDL_Keycode key) {
  g_camera.on_key_pressed(key);
  if (is_descend_key(key)) {
    g_camera.set_descend_active(true);
  }

  if (key == SDLK_C) {
    engine::CameraMode next = (g_camera.get_mode() == engine::CameraMode::ORBIT) ? engine::CameraMode::WASD : engine::CameraMode::ORBIT;
    g_camera.set_mode(next);
    printf("Camera mode: %s\n", next == engine::CameraMode::ORBIT ? "ORBIT" : "WASD");
  } else if (key == SDLK_R) {
    g_camera.reset();
    printf("Camera reset\n");
  } else if (key == SDLK_P) {
    toggle_pointer_lock();
  }
}

static void handle_camera_key_up(SDL_Keycode key) {
  g_camera.on_key_released(key);
  if (is_descend_key(key)) {
    g_camera.set_descend_active(false);
  }
}

// Poll SDL events: quit, keyboard, mouse. Updates the shared camera.
// Works identically in native and Emscripten (SDL abstracts the event source).
static void process_input(bool& running) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;

      case SDL_EVENT_KEY_DOWN:
        handle_camera_key_down(event.key.key);
        break;

      case SDL_EVENT_KEY_UP:
        handle_camera_key_up(event.key.key);
        break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        g_mouse_look_active = true;
        g_mouse_drag_action
            = (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) ? MouseDragAction::Pan : MouseDragAction::Rotate;
        break;

      case SDL_EVENT_MOUSE_BUTTON_UP:
        g_mouse_look_active = false;
        g_mouse_drag_action = MouseDragAction::None;
        break;

      case SDL_EVENT_MOUSE_MOTION:
        if (g_mouse_look_active) {
          if (g_mouse_drag_action == MouseDragAction::Pan) {
            g_camera.on_mouse_pan(event.motion.xrel, event.motion.yrel);
          } else {
            g_camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
          }
        }
        break;

      case SDL_EVENT_MOUSE_WHEEL:
        g_camera.on_mouse_wheel(event.wheel.y);
        break;
    }
  }
}

#ifdef USE_WEBGPU
// ============================================================================
// G3: WebGPU BUILD — Dawn (native) / emdawnwebgpu (web) + Dear ImGui
// ============================================================================
//
// The device, surface and per-frame render pass live HERE in C++ now (WebGPU
// C API), identical on native (Dawn / D3D12·Metal·Vulkan) and web
// (emdawnwebgpu). Adapted from master's core/Window.cpp + Engine::Tick; this
// replaces the earlier JS-shell stub. For now it clears the surface and drives
// an ImGui control panel — proving the full SDL3 -> Dawn -> ImGui path end to
// end. The DICOM ray-cast (WGSL) lands on top of this host in a later step.

// Current state: this host now records a WGSL volume pass from RenderBridge
// commands, then draws ImGui as an overlay. The next resource step is replacing
// the synthetic phantom bytes with real DICOM loader output.
#  include <webgpu/webgpu.h>
#  include <imgui.h>
#  include <imgui_impl_sdl3.h>
#  include <imgui_impl_wgpu.h>
#  include "render_bridge.hpp"
#  include "transform_system.hpp"
#  include "volume_buffer.h"
#  include "volume_file.h"
#  include "embedded_shaders.h"
#  ifdef HAVE_GDCM
#    include "volume_io.h"
#  endif
#  ifdef __EMSCRIPTEN__
#    include <emscripten/html5.h>
#  endif
#  if defined(SDL_PLATFORM_APPLE)
#    include <SDL3/SDL_metal.h>
#  endif

namespace {

  // Dawn's wgpuInstanceRequestAdapter / RequestDevice are async even on native.
  // Pump events until the callback fires (emscripten_sleep yields to JS on web).
  struct AdapterReq {
    WGPUAdapter adapter = nullptr;
    bool done = false;
  };
  void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void* ud1, void*) {
    auto* r = static_cast<AdapterReq*>(ud1);
    if (status == WGPURequestAdapterStatus_Success)
      r->adapter = adapter;
    else
      SDL_Log("RequestAdapter failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
    r->done = true;
  }
  struct DeviceReq {
    WGPUDevice device = nullptr;
    bool done = false;
  };
  void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void* ud1, void*) {
    auto* r = static_cast<DeviceReq*>(ud1);
    if (status == WGPURequestDeviceStatus_Success)
      r->device = device;
    else
      SDL_Log("RequestDevice failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
    r->done = true;
  }
  struct MapReq {
    bool done = false;
    bool ok = false;
  };
  void onBufferMapped(WGPUMapAsyncStatus status, WGPUStringView msg, void* ud1, void*) {
    auto* r = static_cast<MapReq*>(ud1);
    r->ok = status == WGPUMapAsyncStatus_Success;
    if (!r->ok) {
      SDL_Log("Buffer map failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
    }
    r->done = true;
  }
  void onUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
    SDL_Log("[WGPU error type=%d]: %.*s", (int)type, (int)msg.length, msg.data ? msg.data : "");
  }

  void reportStartupStatus(const char* kind, const char* message) {
    if (std::strcmp(kind, "error") == 0) {
      SDL_Log("%s: %s", kind, message);
    } else {
      printf("[%s] %s\n", kind, message);
    }
#  ifdef __EMSCRIPTEN__
    EM_ASM(
        {
          const kind = UTF8ToString($0);
          const message = UTF8ToString($1);
          if (globalThis.mobagenSetStatus) globalThis.mobagenSetStatus(kind, message);
        },
        kind, message);
#  endif
  }

  bool pumpUntil(WGPUInstance inst, bool& flag, const char* operation, Uint64 timeoutMs = 10000) {
    const Uint64 start = SDL_GetTicks();
    while (!flag) {
      // Emdawn marks adapter/device futures ready from JavaScript promises,
      // but callbacks using AllowProcessEvents are delivered only when the
      // app pumps WebGPU events. Without this call the browser build sits on
      // the CSS-blue canvas forever: requestAdapter resolved, but our C++
      // onAdapter/onDevice callback never runs.
      wgpuInstanceProcessEvents(inst);
      if (SDL_GetTicks() - start > timeoutMs) {
        SDL_Log("%s timed out after %llu ms", operation, static_cast<unsigned long long>(timeoutMs));
        return false;
      }
#  ifdef __EMSCRIPTEN__
      emscripten_sleep(1);
#  else
      SDL_Delay(1);
#  endif
    }
    return true;
  }

  // Uniform buffers in WebGPU are read in 16-byte chunks. These tiny structs make
  // that ABI rule visible in code: vec4f and vec4u are the smallest safe packets.
  struct alignas(16) GpuVec4f {
    float x, y, z, w;
  };

  struct alignas(16) GpuVec4u {
    std::uint32_t x, y, z, w;
  };

  struct alignas(16) GpuHistogramParams {
    GpuVec4u dims;
    GpuVec4u mode;
  };

  static constexpr std::uint32_t kHistogramBinsR8 = 256u;
  static constexpr std::uint32_t kHistogramBinsU16 = 65536u;

  static std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment) { return (value + alignment - 1u) & ~(alignment - 1u); }

  static WGPUBuffer createBuffer(WGPUDevice device, const char* label, std::uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = {label, WGPU_STRLEN};
    desc.size = size;
    desc.usage = usage;
    return wgpuDeviceCreateBuffer(device, &desc);
  }

  static std::vector<unsigned char> makePhantomVolume(int n) {
    std::vector<unsigned char> v(static_cast<std::size_t>(n) * n * n);
    for (int z = 0; z < n; ++z) {
      for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
          const glm::vec3 p = (glm::vec3(x, y, z) / static_cast<float>(n - 1) - 0.5f) * 2.0f;

          // A phantom is not "real DICOM"; it is predictable input for
          // validating the renderer. Core + shell + two small lobes make
          // camera motion and transfer functions easier to see than a
          // single flat sphere.
          const float r = glm::length(p);
          float density = glm::smoothstep(0.95f, 0.10f, r) * 0.55f;
          density += glm::smoothstep(0.62f, 0.54f, std::abs(r - 0.62f)) * 0.35f;
          density += glm::smoothstep(0.22f, 0.02f, glm::length(p - glm::vec3(-0.24f, 0.05f, 0.10f))) * 0.45f;
          density += glm::smoothstep(0.18f, 0.02f, glm::length(p - glm::vec3(0.26f, 0.02f, -0.12f))) * 0.38f;
          density = glm::clamp(density, 0.0f, 1.0f);

          v[(static_cast<std::size_t>(z) * n + y) * n + x] = static_cast<unsigned char>(density * 255.0f);
        }
      }
    }
    return v;
  }

  static volume::VolumeBuffer makePhantomVolumeBuffer() {
    constexpr std::uint32_t n = 96;
    volume::VolumeMetadata meta;
    meta.width = n;
    meta.height = n;
    meta.depth = n;
    meta.spacing_mm = {1.0f, 1.0f, 1.5f};
    meta.window_center = 0.5f;
    meta.window_width = 1.0f;
    meta.value_min = 0.0f;
    meta.value_max = 255.0f;
    std::vector<unsigned char> bytes = makePhantomVolume(static_cast<int>(n));
    return volume::VolumeBuffer::from_u8(meta, bytes.data());
  }

  static volume::VolumeBuffer tryLoadDicomVolumeBuffer(bool& loadedFromDicom) {
    loadedFromDicom = false;
#  ifdef HAVE_GDCM
#    ifdef MOBAGEN_DICOM_PATH
    const char* dicomDir = MOBAGEN_DICOM_PATH;
#    else
    const char* dicomDir = "apps/dicom_viewer/assets/dicom";
#    endif
    VolumeData dicom = volume_io_load_series(dicomDir);
    if (!dicom.voxels) {
      printf("DICOM load skipped/failed at %s; using synthetic phantom\n", dicomDir);
      return {};
    }

    volume::VolumeMetadata meta;
    meta.width = static_cast<std::uint32_t>(dicom.width);
    meta.height = static_cast<std::uint32_t>(dicom.height);
    meta.depth = static_cast<std::uint32_t>(dicom.depth);
    meta.spacing_mm = {dicom.spacing_x, dicom.spacing_y, dicom.spacing_z};
    meta.rescale_slope = dicom.rescale_slope;
    meta.rescale_intercept = dicom.rescale_intercept;
    meta.window_center = dicom.window_center;
    meta.window_width = dicom.window_width;
    meta.value_min = dicom.value_min;
    meta.value_max = dicom.value_max;

    volume::VolumeBuffer buffer = volume::VolumeBuffer::from_u16_packed_rg8(meta, dicom.voxels);
    printf("Loaded DICOM volume %ux%ux%u from %s -> packed UInt16 RG8 upload\n", meta.width, meta.height, meta.depth, dicomDir);
    volume_io_free(&dicom);
    loadedFromDicom = !buffer.empty();
    return buffer;
#  else
    return {};
#  endif
  }

  static std::vector<unsigned char> makeTransferLut(std::uint32_t preset) {
    std::vector<unsigned char> lut(256 * 4);
    for (int i = 0; i < 256; ++i) {
      const float t = i / 255.0f;
      glm::vec3 rgb;
      float a;
      switch (preset) {
        case 2:
          a = glm::smoothstep(0.15f, 0.50f, t);
          rgb = glm::mix(glm::vec3(0.55f, 0.12f, 0.05f), glm::vec3(1.00f, 0.92f, 0.78f), t);
          break;
        case 3:
          a = (t > 0.30f && t < 0.55f) ? 0.9f : 0.0f;
          rgb = glm::vec3(0.2f, 0.9f, 0.6f);
          break;
        case 4:
          a = t;
          rgb = glm::mix(glm::vec3(0.0f, 0.1f, 0.4f), glm::vec3(0.7f, 0.95f, 1.0f), t);
          break;
        default:
          a = t;
          rgb = glm::vec3(t);
          break;
      }
      lut[i * 4 + 0] = static_cast<unsigned char>(glm::clamp(rgb.r, 0.0f, 1.0f) * 255.0f);
      lut[i * 4 + 1] = static_cast<unsigned char>(glm::clamp(rgb.g, 0.0f, 1.0f) * 255.0f);
      lut[i * 4 + 2] = static_cast<unsigned char>(glm::clamp(rgb.b, 0.0f, 1.0f) * 255.0f);
      lut[i * 4 + 3] = static_cast<unsigned char>(glm::clamp(a, 0.0f, 1.0f) * 255.0f);
    }
    return lut;
  }

  static std::vector<unsigned char> padTextureRows(const std::vector<unsigned char>& src, std::uint32_t width, std::uint32_t height,
                                                   std::uint32_t depth, std::uint32_t bytesPerPixel, std::uint32_t& outBytesPerRow) {
    const std::uint32_t tightBytesPerRow = width * bytesPerPixel;
    outBytesPerRow = alignUp(tightBytesPerRow, 256u);
    std::vector<unsigned char> padded(static_cast<std::size_t>(outBytesPerRow) * height * depth);

    for (std::uint32_t z = 0; z < depth; ++z) {
      for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t srcOffset = (static_cast<std::size_t>(z) * height + y) * tightBytesPerRow;
        const std::size_t dstOffset = (static_cast<std::size_t>(z) * height + y) * outBytesPerRow;
        std::memcpy(padded.data() + dstOffset, src.data() + srcOffset, tightBytesPerRow);
      }
    }
    return padded;
  }

  static std::uint32_t modeToGpu(render::VolumeRenderMode mode) {
    switch (mode) {
      case render::VolumeRenderMode::MIP:
        return 1u;
      case render::VolumeRenderMode::Isosurface:
        return 2u;
      case render::VolumeRenderMode::DVR:
      default:
        return 0u;
    }
  }

  static std::uint32_t scalarFormatToGpu(render::VolumeScalarFormat format) {
    switch (format) {
      case render::VolumeScalarFormat::UInt16:
        return 1u;
      case render::VolumeScalarFormat::UInt8:
      default:
        return 0u;
    }
  }

  static std::uint32_t histogramBinsForFormat(render::VolumeScalarFormat format) {
    return format == render::VolumeScalarFormat::UInt16 ? kHistogramBinsU16 : kHistogramBinsR8;
  }

  static float histogramBinToScalar(std::uint32_t bin, std::uint32_t binCount, render::VolumeScalarFormat format) {
    if (format == render::VolumeScalarFormat::UInt16) {
      return static_cast<float>(bin);
    }
    const float denom = static_cast<float>(glm::max(binCount, 2u) - 1u);
    return static_cast<float>(bin) / denom;
  }

  static std::uint32_t percentileBin(const std::vector<std::uint32_t>& bins, double percentile, std::uint64_t total) {
    if (bins.empty() || total == 0) return 0;
    const std::uint64_t target = static_cast<std::uint64_t>(glm::clamp(percentile, 0.0, 1.0) * static_cast<double>(total - 1));
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < bins.size(); ++i) {
      sum += bins[i];
      if (sum > target) return i;
    }
    return static_cast<std::uint32_t>(bins.size() - 1);
  }

  static glm::vec3 boxHalfFromSource(const render::VolumeSource& source) {
    glm::vec3 dims(static_cast<float>(source.width), static_cast<float>(source.height), static_cast<float>(source.depth));
    glm::vec3 physical = dims * source.spacing_mm;
    const float longest = glm::max(physical.x, glm::max(physical.y, physical.z));
    if (longest <= 0.0f) return glm::vec3(1.0f);
    return physical / longest;
  }

}  // namespace

struct AppWebGPU {
  SDL_Window* window = nullptr;
  bool running = true;

  WGPUInstance instance = nullptr;
  WGPUAdapter adapter = nullptr;
  WGPUDevice device = nullptr;
  WGPUQueue queue = nullptr;
  WGPUSurface surface = nullptr;
  WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
#  if defined(SDL_PLATFORM_APPLE)
  SDL_MetalView metalView = nullptr;
#  endif
  int cfgW = 0, cfgH = 0;
  float clearColor[4] = {0.10f, 0.20f, 0.50f, 1.0f};

  ecs::World world;
  scene::TransformSystem transforms;
  render::RenderBridge renderBridge;
  volume::VolumeBuffer cpuVolume;
  bool cpuVolumeFromDicom = false;

  WGPUShaderModule volumeShader = nullptr;
  WGPUBindGroupLayout volumeBindGroupLayout = nullptr;
  WGPUPipelineLayout volumePipelineLayout = nullptr;
  WGPURenderPipeline volumePipeline = nullptr;
  WGPUBuffer fullscreenVbo = nullptr;
  WGPUBuffer cameraBuffer = nullptr;
  WGPUBuffer modeBuffer = nullptr;
  WGPUBuffer windowBuffer = nullptr;
  WGPUBuffer boxHalfBuffer = nullptr;
  WGPUSampler volumeSampler = nullptr;
  WGPUTexture volumeTexture = nullptr;
  WGPUTextureView volumeTextureView = nullptr;
  WGPUTexture transferTexture = nullptr;
  WGPUTextureView transferTextureView = nullptr;
  WGPUBindGroup volumeBindGroup = nullptr;
  WGPUShaderModule histogramShader = nullptr;
  WGPUBindGroupLayout histogramBindGroupLayout = nullptr;
  WGPUPipelineLayout histogramPipelineLayout = nullptr;
  WGPUComputePipeline histogramPipeline = nullptr;
  WGPUBuffer histogramBuffer = nullptr;
  WGPUBuffer histogramReadbackBuffer = nullptr;
  WGPUBuffer histogramParamsBuffer = nullptr;
  WGPUBindGroup histogramBindGroup = nullptr;
  std::uint32_t histogramBinCount = 0;
  bool histogramAvailable = false;
  std::uint64_t histogramTotal = 0;
  std::uint32_t histogramLowBin = 0;
  std::uint32_t histogramHighBin = 0;
  float histogramLowValue = 0.0f;
  float histogramHighValue = 0.0f;
  std::string histogramStatus = "GPU histogram not run yet.";
  std::uint32_t uploadedTransferPreset = 0;
  std::uint32_t debugMode = 0;      // 0 final, 1 ray dir, 2 depth, 3 samples
  std::uint32_t sampleSteps = 128;  // ray-march samples; quality/cost knob
  float opacityScale = 0.20f;       // per-sample opacity multiplier

  bool init();
  void tick();
  void cleanup();

private:
  void createSurface();
  bool initDeviceAndQueue();
  void configureSurface(int w, int h);
  void createStudyVolumeScene();
  bool initVolumeRenderer();
  bool initHistogramResources(const render::VolumeSource& source);
  bool runGpuHistogramAutoWindow(render::VolumeRenderable& volume);
  void uploadTransferLut(std::uint32_t preset);
  void drawVolume(WGPURenderPassEncoder pass);
  void releaseVolumeRenderer();
};

void AppWebGPU::createSurface() {
  WGPUSurfaceDescriptor desc = {};
#  if defined(__EMSCRIPTEN__)
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
  canvasDesc.selector = {"#canvas", WGPU_STRLEN};
  desc.nextInChain = &canvasDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#  elif defined(SDL_PLATFORM_WIN32)
  WGPUSurfaceSourceWindowsHWND hwndDesc = {};
  hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
  hwndDesc.hinstance = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  hwndDesc.hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  desc.nextInChain = &hwndDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#  elif defined(SDL_PLATFORM_APPLE)
  metalView = SDL_Metal_CreateView(window);
  WGPUSurfaceSourceMetalLayer metalDesc = {};
  metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
  metalDesc.layer = SDL_Metal_GetLayer(metalView);
  desc.nextInChain = &metalDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#  elif defined(SDL_PLATFORM_LINUX)
  void* xdisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  uint64_t xwindow = (uint64_t)SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  WGPUSurfaceSourceXlibWindow xlibDesc = {};
  xlibDesc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
  xlibDesc.display = xdisplay;
  xlibDesc.window = xwindow;
  desc.nextInChain = &xlibDesc.chain;
  surface = wgpuInstanceCreateSurface(instance, &desc);
#  else
#    error "Unsupported platform for WebGPU surface creation"
#  endif
  if (!surface) fprintf(stderr, "wgpuInstanceCreateSurface failed\n");
}

bool AppWebGPU::initDeviceAndQueue() {
  AdapterReq aReq;
  WGPURequestAdapterOptions aOpts = {};
  aOpts.compatibleSurface = surface;
  aOpts.powerPreference = WGPUPowerPreference_HighPerformance;
  WGPURequestAdapterCallbackInfo aCb = {};
  aCb.mode = WGPUCallbackMode_AllowProcessEvents;
  aCb.callback = onAdapter;
  aCb.userdata1 = &aReq;
  reportStartupStatus("loading", "Requesting WebGPU adapter...");
  wgpuInstanceRequestAdapter(instance, &aOpts, aCb);
  if (!pumpUntil(instance, aReq.done, "requestAdapter")) {
    reportStartupStatus("error", "Timed out while requesting a WebGPU adapter.");
    return false;
  }
  if (!aReq.adapter) {
    reportStartupStatus("error", "No WebGPU adapter is available in this browser or GPU configuration.");
    fprintf(stderr, "No WebGPU adapter\n");
    return false;
  }
  adapter = aReq.adapter;

  DeviceReq dReq;
  WGPUDeviceDescriptor dDesc = {};
  dDesc.label = {"dicom_renderer device", WGPU_STRLEN};
  dDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
  WGPURequestDeviceCallbackInfo dCb = {};
  dCb.mode = WGPUCallbackMode_AllowProcessEvents;
  dCb.callback = onDevice;
  dCb.userdata1 = &dReq;
  reportStartupStatus("loading", "Requesting WebGPU device...");
  wgpuAdapterRequestDevice(adapter, &dDesc, dCb);
  if (!pumpUntil(instance, dReq.done, "requestDevice")) {
    reportStartupStatus("error", "Timed out while requesting a WebGPU device.");
    return false;
  }
  if (!dReq.device) {
    reportStartupStatus("error", "WebGPU adapter was found, but device creation failed.");
    fprintf(stderr, "WebGPU device request failed\n");
    return false;
  }
  device = dReq.device;
  queue = wgpuDeviceGetQueue(device);

  WGPUSurfaceCapabilities caps = {};
  wgpuSurfaceGetCapabilities(surface, adapter, &caps);
  surfaceFormat = (caps.formatCount > 0 && caps.formats) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
  wgpuSurfaceCapabilitiesFreeMembers(caps);
  printf("WebGPU device ready (surfaceFormat=%d)\n", (int)surfaceFormat);
  return true;
}

void AppWebGPU::configureSurface(int w, int h) {
  if (w <= 0 || h <= 0) return;
  WGPUSurfaceConfiguration cfg = {};
  cfg.device = device;
  cfg.format = surfaceFormat;
  cfg.usage = WGPUTextureUsage_RenderAttachment;
  cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
  cfg.width = (uint32_t)w;
  cfg.height = (uint32_t)h;
  cfg.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(surface, &cfg);
  cfgW = w;
  cfgH = h;
}

void AppWebGPU::createStudyVolumeScene() {
  // This is the first live DOD -> renderer handoff:
  //   Entity + Transform + VolumeRenderable
  // becomes, every frame:
  //   VolumeDrawCommand[] consumed by the renderer host.
  //
  // The WebGPU host still only clears + draws ImGui. The important step here
  // is architectural: the renderer no longer needs to query ECS storage while
  // recording GPU commands. It receives a flat command list.
  cpuVolume = tryLoadDicomVolumeBuffer(cpuVolumeFromDicom);
  if (cpuVolume.empty()) {
    // Web (and native without GDCM): load the offline-converted DICOM volume.
    // scripts/dicom_to_mvol.py turns the series into apps/dicom_viewer/assets/volume.mvol (packed
    // UInt16 RG8 + metadata); the wasm build preloads it into the FS. This is
    // what brings REAL DICOM intensities (GPU window/level + histogram) to the
    // browser, where GDCM is unavailable.
#  ifdef __EMSCRIPTEN__
    const char* mvolPath = "/volume.mvol";
#  elif defined(MOBAGEN_MVOL_PATH)
    const char* mvolPath = MOBAGEN_MVOL_PATH;
#  else
    const char* mvolPath = "apps/dicom_viewer/assets/volume.mvol";
#  endif
    bool loadedFromFile = false;
    volume::VolumeBuffer fileVolume = volume::load_volume_file(mvolPath, loadedFromFile);
    if (loadedFromFile) {
      cpuVolume = std::move(fileVolume);
      cpuVolumeFromDicom = true;  // real intensities -> UInt16 GPU windowing path
      printf("Loaded DICOM volume from %s -> packed UInt16 RG8 upload\n", mvolPath);
    }
  }
  if (cpuVolume.empty()) {
    cpuVolume = makePhantomVolumeBuffer();
    cpuVolumeFromDicom = false;
  }
  const volume::VolumeMetadata& meta = cpuVolume.metadata();

  ecs::Entity phantom = world.create();

  scene::Transform t;
  t.scale = {1.0f, 1.0f, 1.0f};
  world.add<scene::Transform>(phantom, t);

  render::VolumeRenderable volume;
  volume.source.id = 1;
  volume.source.width = meta.width;
  volume.source.height = meta.height;
  volume.source.depth = meta.depth;
  volume.source.spacing_mm = meta.spacing_mm;
  volume.source.format = cpuVolume.storage_format() == ::volume::VolumeStorageFormat::U16PackedRG8 ? render::VolumeScalarFormat::UInt16
                                                                                                   : render::VolumeScalarFormat::UInt8;

  if (volume.source.format == render::VolumeScalarFormat::UInt16) {
    // We preserve the DICOM stored UInt16 values on upload. The shader now
    // reconstructs those values and applies window/level on the GPU. Window
    // metadata arrives in HU, so convert the center/width to stored-value
    // units for the current shader packet:
    //
    //   HU = stored*slope + intercept
    //   stored_center = (HU_center - intercept) / slope
    //   stored_width  = HU_width / abs(slope)
    const float slope = std::abs(meta.rescale_slope) > 0.0001f ? meta.rescale_slope : 1.0f;
    volume.display.window_center = (meta.window_center - meta.rescale_intercept) / slope;
    volume.display.window_width = glm::max(meta.window_width / std::abs(slope), 1.0f);
  } else {
    volume.display.window_center = 0.5f;
    volume.display.window_width = 1.0f;
  }
  volume.display.transfer_preset = cpuVolumeFromDicom ? 2u : 1u;
  volume.display.mode = render::VolumeRenderMode::DVR;
  world.add<render::VolumeRenderable>(phantom, volume);

  transforms.rebuild(world);
}

bool AppWebGPU::initVolumeRenderer() {
  WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
  wgsl.code = {shaders::RAYGEN_WGSL, WGPU_STRLEN};
  WGPUShaderModuleDescriptor shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  shaderDesc.label = {"volume raygen.wgsl", WGPU_STRLEN};
  shaderDesc.nextInChain = &wgsl.chain;
  volumeShader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
  if (!volumeShader) {
    fprintf(stderr, "Failed to create WGSL shader module\n");
    return false;
  }

  // Fullscreen triangle list: the vertex shader only needs clip-space xy and
  // uv. Every pixel in the surface runs the ray-marching fragment shader.
  const float quad[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,

      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
  };
  fullscreenVbo = createBuffer(device, "fullscreen volume quad", sizeof(quad), WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
  cameraBuffer = createBuffer(device, "camera inv view-projection", sizeof(glm::mat4), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
  modeBuffer = createBuffer(device, "volume mode", sizeof(GpuVec4u), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
  windowBuffer = createBuffer(device, "window level", sizeof(GpuVec4f), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
  boxHalfBuffer = createBuffer(device, "volume box half extents", sizeof(GpuVec4f), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
  if (!fullscreenVbo || !cameraBuffer || !modeBuffer || !windowBuffer || !boxHalfBuffer) {
    fprintf(stderr, "Failed to create WebGPU buffers\n");
    return false;
  }
  wgpuQueueWriteBuffer(queue, fullscreenVbo, 0, quad, sizeof(quad));

  const auto& commands = renderBridge.volume_commands();
  const render::VolumeSource source = commands.empty() ? render::VolumeSource{1u, 96u, 96u, 96u, glm::vec3(1.0f, 1.0f, 1.5f)} : commands[0].source;
  const bool packedU16
      = source.format == render::VolumeScalarFormat::UInt16 && cpuVolume.storage_format() == ::volume::VolumeStorageFormat::U16PackedRG8;
  const std::uint32_t bytesPerVoxel = packedU16 ? 2u : 1u;
  const WGPUTextureFormat volumeFormat = packedU16 ? WGPUTextureFormat_RG8Unorm : WGPUTextureFormat_R8Unorm;

  WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
  samplerDesc.label = {packedU16 ? "volume nearest sampler" : "volume linear sampler", WGPU_STRLEN};
  samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
  samplerDesc.magFilter = packedU16 ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
  samplerDesc.minFilter = packedU16 ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
  samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
  volumeSampler = wgpuDeviceCreateSampler(device, &samplerDesc);
  if (!volumeSampler) {
    fprintf(stderr, "Failed to create volume sampler\n");
    return false;
  }

  WGPUTextureDescriptor volumeDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
  volumeDesc.label = {packedU16 ? "DICOM volume UInt16 packed RG8" : "volume R8", WGPU_STRLEN};
  volumeDesc.dimension = WGPUTextureDimension_3D;
  volumeDesc.size.width = source.width;
  volumeDesc.size.height = source.height;
  volumeDesc.size.depthOrArrayLayers = source.depth;
  volumeDesc.format = volumeFormat;
  volumeDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  volumeTexture = wgpuDeviceCreateTexture(device, &volumeDesc);
  if (!volumeTexture) {
    fprintf(stderr, "Failed to create 3D volume texture\n");
    return false;
  }

  std::vector<unsigned char> voxels;
  if (!cpuVolume.empty()) {
    voxels.assign(cpuVolume.data(), cpuVolume.data() + cpuVolume.size_bytes());
  } else {
    voxels = makePhantomVolume(static_cast<int>(source.width));
  }
  std::uint32_t volumeBytesPerRow = 0;
  std::vector<unsigned char> paddedVoxels = padTextureRows(voxels, source.width, source.height, source.depth, bytesPerVoxel, volumeBytesPerRow);
  WGPUTexelCopyTextureInfo volumeDst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  volumeDst.texture = volumeTexture;
  volumeDst.aspect = WGPUTextureAspect_All;
  WGPUTexelCopyBufferLayout volumeLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  volumeLayout.bytesPerRow = volumeBytesPerRow;
  volumeLayout.rowsPerImage = source.height;
  WGPUExtent3D volumeWrite = WGPU_EXTENT_3D_INIT;
  volumeWrite.width = source.width;
  volumeWrite.height = source.height;
  volumeWrite.depthOrArrayLayers = source.depth;
  wgpuQueueWriteTexture(queue, &volumeDst, paddedVoxels.data(), paddedVoxels.size(), &volumeLayout, &volumeWrite);

  WGPUTextureViewDescriptor volumeViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  volumeViewDesc.label = {packedU16 ? "DICOM packed UInt16 volume view" : "R8 volume view", WGPU_STRLEN};
  volumeViewDesc.format = volumeFormat;
  volumeViewDesc.dimension = WGPUTextureViewDimension_3D;
  volumeViewDesc.mipLevelCount = 1;
  volumeViewDesc.arrayLayerCount = 1;
  volumeViewDesc.aspect = WGPUTextureAspect_All;
  volumeViewDesc.usage = WGPUTextureUsage_TextureBinding;
  volumeTextureView = wgpuTextureCreateView(volumeTexture, &volumeViewDesc);

  WGPUTextureDescriptor transferDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
  transferDesc.label = {"transfer LUT RGBA8", WGPU_STRLEN};
  transferDesc.dimension = WGPUTextureDimension_2D;
  transferDesc.size.width = 256;
  transferDesc.size.height = 1;
  transferDesc.size.depthOrArrayLayers = 1;
  transferDesc.format = WGPUTextureFormat_RGBA8Unorm;
  transferDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  transferTexture = wgpuDeviceCreateTexture(device, &transferDesc);
  if (!transferTexture) {
    fprintf(stderr, "Failed to create transfer LUT texture\n");
    return false;
  }

  WGPUTextureViewDescriptor transferViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  transferViewDesc.label = {"transfer LUT view", WGPU_STRLEN};
  transferViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  transferViewDesc.dimension = WGPUTextureViewDimension_2D;
  transferViewDesc.mipLevelCount = 1;
  transferViewDesc.arrayLayerCount = 1;
  transferViewDesc.aspect = WGPUTextureAspect_All;
  transferViewDesc.usage = WGPUTextureUsage_TextureBinding;
  transferTextureView = wgpuTextureCreateView(transferTexture, &transferViewDesc);
  uploadTransferLut(1u);

  if (!volumeTextureView || !transferTextureView) {
    fprintf(stderr, "Failed to create texture views\n");
    return false;
  }

  WGPUBindGroupLayoutEntry layoutEntries[7];
  for (WGPUBindGroupLayoutEntry& e : layoutEntries) {
    e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  }

  layoutEntries[0].binding = 0;
  layoutEntries[0].visibility = WGPUShaderStage_Fragment;
  layoutEntries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
  layoutEntries[0].buffer.minBindingSize = sizeof(glm::mat4);

  layoutEntries[1].binding = 1;
  layoutEntries[1].visibility = WGPUShaderStage_Fragment;
  layoutEntries[1].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
  layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
  layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_3D;

  layoutEntries[2].binding = 2;
  layoutEntries[2].visibility = WGPUShaderStage_Fragment;
  layoutEntries[2].sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
  layoutEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

  layoutEntries[3].binding = 3;
  layoutEntries[3].visibility = WGPUShaderStage_Fragment;
  layoutEntries[3].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
  layoutEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
  layoutEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

  layoutEntries[4].binding = 4;
  layoutEntries[4].visibility = WGPUShaderStage_Fragment;
  layoutEntries[4].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  layoutEntries[4].buffer.type = WGPUBufferBindingType_Uniform;
  layoutEntries[4].buffer.minBindingSize = sizeof(GpuVec4u);

  layoutEntries[5].binding = 5;
  layoutEntries[5].visibility = WGPUShaderStage_Fragment;
  layoutEntries[5].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  layoutEntries[5].buffer.type = WGPUBufferBindingType_Uniform;
  layoutEntries[5].buffer.minBindingSize = sizeof(GpuVec4f);

  layoutEntries[6].binding = 6;
  layoutEntries[6].visibility = WGPUShaderStage_Fragment;
  layoutEntries[6].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  layoutEntries[6].buffer.type = WGPUBufferBindingType_Uniform;
  layoutEntries[6].buffer.minBindingSize = sizeof(GpuVec4f);

  WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  bglDesc.label = {"volume bind group layout", WGPU_STRLEN};
  bglDesc.entryCount = 7;
  bglDesc.entries = layoutEntries;
  volumeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
  if (!volumeBindGroupLayout) {
    fprintf(stderr, "Failed to create volume bind group layout\n");
    return false;
  }

  WGPUBindGroupEntry bgEntries[7];
  for (WGPUBindGroupEntry& e : bgEntries) {
    e = WGPU_BIND_GROUP_ENTRY_INIT;
  }
  bgEntries[0].binding = 0;
  bgEntries[0].buffer = cameraBuffer;
  bgEntries[0].size = sizeof(glm::mat4);
  bgEntries[1].binding = 1;
  bgEntries[1].textureView = volumeTextureView;
  bgEntries[2].binding = 2;
  bgEntries[2].sampler = volumeSampler;
  bgEntries[3].binding = 3;
  bgEntries[3].textureView = transferTextureView;
  bgEntries[4].binding = 4;
  bgEntries[4].buffer = modeBuffer;
  bgEntries[4].size = sizeof(GpuVec4u);
  bgEntries[5].binding = 5;
  bgEntries[5].buffer = windowBuffer;
  bgEntries[5].size = sizeof(GpuVec4f);
  bgEntries[6].binding = 6;
  bgEntries[6].buffer = boxHalfBuffer;
  bgEntries[6].size = sizeof(GpuVec4f);

  WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  bgDesc.label = {"volume bind group", WGPU_STRLEN};
  bgDesc.layout = volumeBindGroupLayout;
  bgDesc.entryCount = 7;
  bgDesc.entries = bgEntries;
  volumeBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
  if (!volumeBindGroup) {
    fprintf(stderr, "Failed to create volume bind group\n");
    return false;
  }

  WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  layoutDesc.label = {"volume pipeline layout", WGPU_STRLEN};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = &volumeBindGroupLayout;
  volumePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
  if (!volumePipelineLayout) {
    fprintf(stderr, "Failed to create volume pipeline layout\n");
    return false;
  }

  WGPUVertexAttribute attributes[2];
  attributes[0] = WGPU_VERTEX_ATTRIBUTE_INIT;
  attributes[0].format = WGPUVertexFormat_Float32x2;
  attributes[0].offset = 0;
  attributes[0].shaderLocation = 0;
  attributes[1] = WGPU_VERTEX_ATTRIBUTE_INIT;
  attributes[1].format = WGPUVertexFormat_Float32x2;
  attributes[1].offset = 2 * sizeof(float);
  attributes[1].shaderLocation = 1;

  WGPUVertexBufferLayout vertexLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
  vertexLayout.arrayStride = 4 * sizeof(float);
  vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
  vertexLayout.attributeCount = 2;
  vertexLayout.attributes = attributes;

  WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
  colorTarget.format = surfaceFormat;
  colorTarget.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
  fragment.module = volumeShader;
  fragment.entryPoint = {"fs_main", WGPU_STRLEN};
  fragment.targetCount = 1;
  fragment.targets = &colorTarget;

  WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  pipelineDesc.label = {"volume ray-march pipeline", WGPU_STRLEN};
  pipelineDesc.layout = volumePipelineLayout;
  pipelineDesc.vertex.module = volumeShader;
  pipelineDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
  pipelineDesc.vertex.bufferCount = 1;
  pipelineDesc.vertex.buffers = &vertexLayout;
  pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
  pipelineDesc.primitive.cullMode = WGPUCullMode_None;
  pipelineDesc.fragment = &fragment;
  volumePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
  if (!volumePipeline) {
    fprintf(stderr, "Failed to create volume render pipeline\n");
    return false;
  }

  if (!initHistogramResources(source)) {
    fprintf(stderr, "Failed to create GPU histogram resources\n");
    return false;
  }

  printf("WebGPU volume renderer initialized (%ux%ux%u %s, %s)\n", source.width, source.height, source.depth,
         cpuVolumeFromDicom ? "DICOM" : "phantom", packedU16 ? "packed UInt16 RG8" : "R8 normalized");
  return true;
}

bool AppWebGPU::initHistogramResources(const render::VolumeSource& source) {
  histogramBinCount = histogramBinsForFormat(source.format);
  const std::uint64_t histogramBytes = static_cast<std::uint64_t>(histogramBinCount) * sizeof(std::uint32_t);

  WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
  wgsl.code = {shaders::HISTOGRAM_WGSL, WGPU_STRLEN};
  WGPUShaderModuleDescriptor shaderDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  shaderDesc.label = {"volume histogram.wgsl", WGPU_STRLEN};
  shaderDesc.nextInChain = &wgsl.chain;
  histogramShader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
  if (!histogramShader) return false;

  histogramBuffer
      = createBuffer(device, "GPU histogram bins", histogramBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
  histogramReadbackBuffer = createBuffer(device, "GPU histogram readback", histogramBytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
  histogramParamsBuffer = createBuffer(device, "GPU histogram params", sizeof(GpuHistogramParams), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
  if (!histogramBuffer || !histogramReadbackBuffer || !histogramParamsBuffer) {
    return false;
  }

  WGPUBindGroupLayoutEntry layoutEntries[3];
  for (WGPUBindGroupLayoutEntry& e : layoutEntries) {
    e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  }

  layoutEntries[0].binding = 0;
  layoutEntries[0].visibility = WGPUShaderStage_Compute;
  layoutEntries[0].texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
  layoutEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
  layoutEntries[0].texture.viewDimension = WGPUTextureViewDimension_3D;

  layoutEntries[1].binding = 1;
  layoutEntries[1].visibility = WGPUShaderStage_Compute;
  layoutEntries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  layoutEntries[1].buffer.type = WGPUBufferBindingType_Storage;
  layoutEntries[1].buffer.minBindingSize = histogramBytes;

  layoutEntries[2].binding = 2;
  layoutEntries[2].visibility = WGPUShaderStage_Compute;
  layoutEntries[2].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
  layoutEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
  layoutEntries[2].buffer.minBindingSize = sizeof(GpuHistogramParams);

  WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  bglDesc.label = {"histogram bind group layout", WGPU_STRLEN};
  bglDesc.entryCount = 3;
  bglDesc.entries = layoutEntries;
  histogramBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
  if (!histogramBindGroupLayout) return false;

  WGPUBindGroupEntry bgEntries[3];
  for (WGPUBindGroupEntry& e : bgEntries) {
    e = WGPU_BIND_GROUP_ENTRY_INIT;
  }
  bgEntries[0].binding = 0;
  bgEntries[0].textureView = volumeTextureView;
  bgEntries[1].binding = 1;
  bgEntries[1].buffer = histogramBuffer;
  bgEntries[1].size = histogramBytes;
  bgEntries[2].binding = 2;
  bgEntries[2].buffer = histogramParamsBuffer;
  bgEntries[2].size = sizeof(GpuHistogramParams);

  WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  bgDesc.label = {"histogram bind group", WGPU_STRLEN};
  bgDesc.layout = histogramBindGroupLayout;
  bgDesc.entryCount = 3;
  bgDesc.entries = bgEntries;
  histogramBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
  if (!histogramBindGroup) return false;

  WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  layoutDesc.label = {"histogram pipeline layout", WGPU_STRLEN};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = &histogramBindGroupLayout;
  histogramPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
  if (!histogramPipelineLayout) return false;

  WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
  pipelineDesc.label = {"volume histogram compute pipeline", WGPU_STRLEN};
  pipelineDesc.layout = histogramPipelineLayout;
  pipelineDesc.compute.module = histogramShader;
  pipelineDesc.compute.entryPoint = {"cs_main", WGPU_STRLEN};
  histogramPipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
  if (!histogramPipeline) return false;

  histogramStatus = source.format == render::VolumeScalarFormat::UInt16 ? "GPU histogram ready: 65,536 UInt16 stored-value bins."
                                                                        : "GPU histogram ready: 256 normalized R8 bins.";
  return true;
}

bool AppWebGPU::runGpuHistogramAutoWindow(render::VolumeRenderable& volume) {
  if (!histogramPipeline || !histogramBindGroup || !histogramBuffer || !histogramReadbackBuffer || !histogramParamsBuffer) {
    histogramStatus = "GPU histogram resources are not initialized.";
    return false;
  }

  histogramAvailable = false;
  const std::uint32_t expectedBins = histogramBinsForFormat(volume.source.format);
  if (expectedBins != histogramBinCount) {
    histogramStatus = "GPU histogram bin count does not match this volume format.";
    return false;
  }

  const std::uint64_t histogramBytes = static_cast<std::uint64_t>(histogramBinCount) * sizeof(std::uint32_t);
  const GpuHistogramParams params
      = {{volume.source.width, volume.source.height, volume.source.depth, 0u}, {scalarFormatToGpu(volume.source.format), histogramBinCount, 0u, 0u}};
  wgpuQueueWriteBuffer(queue, histogramParamsBuffer, 0, &params, sizeof(params));

  WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encDesc.label = {"histogram command encoder", WGPU_STRLEN};
  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &encDesc);
  if (!enc) {
    histogramStatus = "Failed to create histogram command encoder.";
    return false;
  }

  // Clear is the "reset histogram" step. Without it, each run would add on
  // top of the previous counts.
  wgpuCommandEncoderClearBuffer(enc, histogramBuffer, 0, histogramBytes);

  WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
  passDesc.label = {"volume histogram compute pass", WGPU_STRLEN};
  WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(enc, &passDesc);
  wgpuComputePassEncoderSetPipeline(pass, histogramPipeline);
  wgpuComputePassEncoderSetBindGroup(pass, 0, histogramBindGroup, 0, nullptr);
  wgpuComputePassEncoderDispatchWorkgroups(pass, alignUp(volume.source.width, 8u) / 8u, alignUp(volume.source.height, 8u) / 8u,
                                           alignUp(volume.source.depth, 4u) / 4u);
  wgpuComputePassEncoderEnd(pass);
  wgpuComputePassEncoderRelease(pass);

  wgpuCommandEncoderCopyBufferToBuffer(enc, histogramBuffer, 0, histogramReadbackBuffer, 0, histogramBytes);
  WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  cbDesc.label = {"histogram command buffer", WGPU_STRLEN};
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cbDesc);
  wgpuQueueSubmit(queue, 1, &cb);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(enc);

  MapReq mapReq;
  WGPUBufferMapCallbackInfo mapCb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
  mapCb.mode = WGPUCallbackMode_AllowProcessEvents;
  mapCb.callback = onBufferMapped;
  mapCb.userdata1 = &mapReq;
  wgpuBufferMapAsync(histogramReadbackBuffer, WGPUMapMode_Read, 0, static_cast<size_t>(histogramBytes), mapCb);
  if (!pumpUntil(instance, mapReq.done, "histogramReadback", 30000)) {
    histogramStatus = "Timed out waiting for GPU histogram readback.";
    return false;
  }
  if (!mapReq.ok) {
    histogramStatus = "GPU histogram readback map failed.";
    return false;
  }

  const void* mapped = wgpuBufferGetConstMappedRange(histogramReadbackBuffer, 0, static_cast<size_t>(histogramBytes));
  if (!mapped) {
    wgpuBufferUnmap(histogramReadbackBuffer);
    histogramStatus = "GPU histogram mapped range was null.";
    return false;
  }

  std::vector<std::uint32_t> bins(histogramBinCount);
  std::memcpy(bins.data(), mapped, static_cast<size_t>(histogramBytes));
  wgpuBufferUnmap(histogramReadbackBuffer);

  histogramTotal = 0;
  for (std::uint32_t count : bins) histogramTotal += count;
  if (histogramTotal == 0) {
    histogramStatus = "GPU histogram returned zero voxels.";
    return false;
  }

  histogramLowBin = percentileBin(bins, 0.01, histogramTotal);
  histogramHighBin = percentileBin(bins, 0.99, histogramTotal);
  if (histogramHighBin <= histogramLowBin) {
    histogramHighBin = glm::min(histogramLowBin + 1u, histogramBinCount - 1u);
  }
  histogramLowValue = histogramBinToScalar(histogramLowBin, histogramBinCount, volume.source.format);
  histogramHighValue = histogramBinToScalar(histogramHighBin, histogramBinCount, volume.source.format);

  const float minWidth = volume.source.format == render::VolumeScalarFormat::UInt16 ? 1.0f : (1.0f / 255.0f);
  volume.display.window_center = (histogramLowValue + histogramHighValue) * 0.5f;
  volume.display.window_width = glm::max(histogramHighValue - histogramLowValue, minWidth);

  char msg[192];
  std::snprintf(msg, sizeof(msg), "GPU histogram auto-window: p01=%.3f p99=%.3f total=%llu bins=%u", histogramLowValue, histogramHighValue,
                static_cast<unsigned long long>(histogramTotal), histogramBinCount);
  histogramStatus = msg;
  histogramAvailable = true;
  printf("%s\n", histogramStatus.c_str());
  return true;
}

void AppWebGPU::uploadTransferLut(std::uint32_t preset) {
  if (!transferTexture) return;
  std::vector<unsigned char> lut = makeTransferLut(preset);
  WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  dst.texture = transferTexture;
  dst.aspect = WGPUTextureAspect_All;
  WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  layout.bytesPerRow = 256u * 4u;
  layout.rowsPerImage = 1;
  WGPUExtent3D writeSize = WGPU_EXTENT_3D_INIT;
  writeSize.width = 256;
  writeSize.height = 1;
  writeSize.depthOrArrayLayers = 1;
  wgpuQueueWriteTexture(queue, &dst, lut.data(), lut.size(), &layout, &writeSize);
  uploadedTransferPreset = preset;
}

void AppWebGPU::drawVolume(WGPURenderPassEncoder pass) {
  const auto& commands = renderBridge.volume_commands();
  if (commands.empty() || !volumePipeline || !volumeBindGroup) return;

  const render::VolumeDrawCommand& cmd = commands[0];
  if (cmd.display.transfer_preset != uploadedTransferPreset) {
    uploadTransferLut(cmd.display.transfer_preset);
  }

  const glm::mat4 invVP = glm::inverse(g_camera.get_view_projection());
  const GpuVec4u mode = {modeToGpu(cmd.display.mode), debugMode, sampleSteps, scalarFormatToGpu(cmd.source.format)};
  const GpuVec4f windowLevel = {cmd.display.window_center, glm::max(cmd.display.window_width, 0.001f), cmd.display.iso_threshold, opacityScale};
  const glm::vec3 half = boxHalfFromSource(cmd.source);
  const GpuVec4f boxHalf = {half.x, half.y, half.z, 0.0f};

  wgpuQueueWriteBuffer(queue, cameraBuffer, 0, glm::value_ptr(invVP), sizeof(glm::mat4));
  wgpuQueueWriteBuffer(queue, modeBuffer, 0, &mode, sizeof(mode));
  wgpuQueueWriteBuffer(queue, windowBuffer, 0, &windowLevel, sizeof(windowLevel));
  wgpuQueueWriteBuffer(queue, boxHalfBuffer, 0, &boxHalf, sizeof(boxHalf));

  wgpuRenderPassEncoderSetPipeline(pass, volumePipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, volumeBindGroup, 0, nullptr);
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, fullscreenVbo, 0, 6u * 4u * sizeof(float));
  wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
}

void AppWebGPU::releaseVolumeRenderer() {
  if (histogramBindGroup) {
    wgpuBindGroupRelease(histogramBindGroup);
    histogramBindGroup = nullptr;
  }
  if (histogramReadbackBuffer) {
    wgpuBufferRelease(histogramReadbackBuffer);
    histogramReadbackBuffer = nullptr;
  }
  if (histogramBuffer) {
    wgpuBufferRelease(histogramBuffer);
    histogramBuffer = nullptr;
  }
  if (histogramParamsBuffer) {
    wgpuBufferRelease(histogramParamsBuffer);
    histogramParamsBuffer = nullptr;
  }
  if (volumeBindGroup) {
    wgpuBindGroupRelease(volumeBindGroup);
    volumeBindGroup = nullptr;
  }
  if (transferTextureView) {
    wgpuTextureViewRelease(transferTextureView);
    transferTextureView = nullptr;
  }
  if (transferTexture) {
    wgpuTextureRelease(transferTexture);
    transferTexture = nullptr;
  }
  if (volumeTextureView) {
    wgpuTextureViewRelease(volumeTextureView);
    volumeTextureView = nullptr;
  }
  if (volumeTexture) {
    wgpuTextureRelease(volumeTexture);
    volumeTexture = nullptr;
  }
  if (volumeSampler) {
    wgpuSamplerRelease(volumeSampler);
    volumeSampler = nullptr;
  }
  if (boxHalfBuffer) {
    wgpuBufferRelease(boxHalfBuffer);
    boxHalfBuffer = nullptr;
  }
  if (windowBuffer) {
    wgpuBufferRelease(windowBuffer);
    windowBuffer = nullptr;
  }
  if (modeBuffer) {
    wgpuBufferRelease(modeBuffer);
    modeBuffer = nullptr;
  }
  if (cameraBuffer) {
    wgpuBufferRelease(cameraBuffer);
    cameraBuffer = nullptr;
  }
  if (fullscreenVbo) {
    wgpuBufferRelease(fullscreenVbo);
    fullscreenVbo = nullptr;
  }
  if (volumePipeline) {
    wgpuRenderPipelineRelease(volumePipeline);
    volumePipeline = nullptr;
  }
  if (volumePipelineLayout) {
    wgpuPipelineLayoutRelease(volumePipelineLayout);
    volumePipelineLayout = nullptr;
  }
  if (volumeBindGroupLayout) {
    wgpuBindGroupLayoutRelease(volumeBindGroupLayout);
    volumeBindGroupLayout = nullptr;
  }
  if (volumeShader) {
    wgpuShaderModuleRelease(volumeShader);
    volumeShader = nullptr;
  }
  if (histogramPipeline) {
    wgpuComputePipelineRelease(histogramPipeline);
    histogramPipeline = nullptr;
  }
  if (histogramPipelineLayout) {
    wgpuPipelineLayoutRelease(histogramPipelineLayout);
    histogramPipelineLayout = nullptr;
  }
  if (histogramBindGroupLayout) {
    wgpuBindGroupLayoutRelease(histogramBindGroupLayout);
    histogramBindGroupLayout = nullptr;
  }
  if (histogramShader) {
    wgpuShaderModuleRelease(histogramShader);
    histogramShader = nullptr;
  }
}

bool AppWebGPU::init() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  int w = 800, h = 600;
#  ifdef __EMSCRIPTEN__
  double cw = 0, ch = 0;
  if (emscripten_get_element_css_size("#canvas", &cw, &ch) == EMSCRIPTEN_RESULT_SUCCESS && cw > 0 && ch > 0) {
    w = (int)cw;
    h = (int)ch;
  }
#  endif
  window = SDL_CreateWindow("DICOM Renderer (WebGPU)", w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  WGPUInstanceDescriptor instDesc = {};
  instance = wgpuCreateInstance(&instDesc);
  if (!instance) {
    reportStartupStatus("error", "Failed to create the WebGPU instance.");
    fprintf(stderr, "wgpuCreateInstance failed\n");
    return false;
  }

  reportStartupStatus("loading", "Creating WebGPU surface...");
  createSurface();
  if (!surface) {
    reportStartupStatus("error", "Failed to create a WebGPU surface for the canvas/window.");
    return false;
  }
  if (!initDeviceAndQueue()) return false;

  int pxW = 0, pxH = 0;
  get_drawable_size(window, pxW, pxH);
  configureSurface(pxW, pxH);
  g_camera.set_viewport(pxW > 0 ? pxW : w, pxH > 0 ? pxH : h);

  ImGui_ImplSDL3_InitForOther(window);
  ImGui_ImplWGPU_InitInfo wgpuInit = {};
  wgpuInit.Device = device;
  wgpuInit.NumFramesInFlight = 3;
  wgpuInit.RenderTargetFormat = surfaceFormat;
  wgpuInit.DepthStencilFormat = WGPUTextureFormat_Undefined;
  if (!ImGui_ImplWGPU_Init(&wgpuInit)) {
    fprintf(stderr, "ImGui_ImplWGPU_Init failed\n");
    return false;
  }

  createStudyVolumeScene();
  renderBridge.build(world);
  if (!initVolumeRenderer()) {
    reportStartupStatus("error", "WebGPU started, but the volume renderer failed to initialize.");
    fprintf(stderr, "WebGPU volume renderer init failed\n");
    return false;
  }

  reportStartupStatus("ready", "WebGPU renderer ready.");

  printf("WebGPU (G3) initialized — Dawn + ImGui (surfaceFormat=%d)\n", (int)surfaceFormat);
  return true;
}

void AppWebGPU::tick() {
  ImGuiIO& io = ImGui::GetIO();
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) running = false;
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) running = false;
    switch (event.type) {
      case SDL_EVENT_KEY_DOWN:
        if (!io.WantCaptureKeyboard) {
          handle_camera_key_down(event.key.key);
        }
        break;
      case SDL_EVENT_KEY_UP:
        if (!io.WantCaptureKeyboard) handle_camera_key_up(event.key.key);
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!io.WantCaptureMouse) {
          g_mouse_look_active = true;
          g_mouse_drag_action = (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) ? MouseDragAction::Pan
                                                                                                                      : MouseDragAction::Rotate;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        g_mouse_look_active = false;
        g_mouse_drag_action = MouseDragAction::None;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        if (g_mouse_look_active && !io.WantCaptureMouse) {
          if (g_mouse_drag_action == MouseDragAction::Pan) {
            g_camera.on_mouse_pan(event.motion.xrel, event.motion.yrel);
          } else {
            g_camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
          }
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!io.WantCaptureMouse) g_camera.on_mouse_wheel(event.wheel.y);
        break;
    }
  }
  g_camera.update(measure_delta_seconds());
  transforms.update(world);
  renderBridge.build(world);

  // Reconfigure the surface on resize (device pixels, HiDPI-aware).
  int pxW = 0, pxH = 0;
  get_drawable_size(window, pxW, pxH);
  if (pxW > 0 && pxH > 0 && (pxW != cfgW || pxH != cfgH)) {
    configureSurface(pxW, pxH);
    g_camera.set_viewport(pxW, pxH);
  }

  WGPUSurfaceTexture st = {};
  wgpuSurfaceGetCurrentTexture(surface, &st);
  bool ok = (st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal || st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
            && st.texture;
  if (!ok) {
    if (st.texture) wgpuTextureRelease(st.texture);
    return;
  }

  WGPUTextureViewDescriptor vd = {};
  vd.format = wgpuTextureGetFormat(st.texture);
  vd.dimension = WGPUTextureViewDimension_2D;
  vd.mipLevelCount = 1;
  vd.arrayLayerCount = 1;
  vd.aspect = WGPUTextureAspect_All;
  WGPUTextureView view = wgpuTextureCreateView(st.texture, &vd);

  WGPUCommandEncoderDescriptor edesc = {};
  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &edesc);

  WGPURenderPassColorAttachment color = {};
  color.view = view;
  color.loadOp = WGPULoadOp_Clear;
  color.storeOp = WGPUStoreOp_Store;
  color.clearValue = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
  color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  WGPURenderPassDescriptor pd = {};
  pd.colorAttachmentCount = 1;
  pd.colorAttachments = &color;
  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pd);

  // Scene pass: consume RenderBridge commands with the WGSL volume pipeline.
  // ImGui is drawn afterwards, so the controls remain a normal overlay.
  drawVolume(pass);

  // ImGui frame (drawn into the same render pass, after any scene geometry).
  ImGui_ImplWGPU_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  {
    ImGui::Begin("DICOM Renderer — WebGPU (Dawn)");
    ImGui::Text("Dawn + ImGui live — %.1f FPS", io.Framerate);
    ImGui::Text("Camera: %s  (press C to toggle)", g_camera.get_mode() == engine::CameraMode::ORBIT ? "ORBIT" : "WASD");
    const glm::vec3 camPos = g_camera.get_position();
    ImGui::Text("Camera pos: %.2f %.2f %.2f", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Yaw/Pitch: %.1f / %.1f deg", g_camera.get_yaw_degrees(), g_camera.get_pitch_degrees());
    if (g_camera.get_mode() == engine::CameraMode::ORBIT) {
      ImGui::Text("Orbit radius: %.2f", g_camera.get_orbit_radius());
      ImGui::TextDisabled("Orbit: left-drag rotate; right/middle-drag pan; wheel zoom.");
    } else {
      ImGui::Text("Move speed: %.2f", g_camera.get_move_speed());
      ImGui::TextDisabled("WASD: move; Space up; Shift/Ctrl down; left-drag look.");
    }
    ImGui::TextDisabled("R resets camera. P requests browser pointer lock.");
    // Projection: live FOV (perspective) or true-to-scale orthographic.
    int proj = (g_camera.get_projection() == engine::Projection::Perspective) ? 0 : 1;
    if (ImGui::Combo("Projection", &proj, "Perspective\0Orthographic\0\0")) {
      g_camera.set_projection(proj == 0 ? engine::Projection::Perspective : engine::Projection::Orthographic);
    }
    if (g_camera.get_projection() == engine::Projection::Perspective) {
      float fov = g_camera.get_fov();
      if (ImGui::SliderFloat("FOV", &fov, 15.0f, 100.0f, "%.0f deg")) g_camera.set_fov(fov);
    }
    ImGui::ColorEdit3("Clear color", clearColor);
    ImGui::SeparatorText("DOD render bridge");
    const auto& volumeCommands = renderBridge.volume_commands();
    ImGui::Text("Volume commands: %d", static_cast<int>(volumeCommands.size()));
    if (!volumeCommands.empty()) {
      const auto& cmd = volumeCommands[0];
      ImGui::Text("Volume id: %u", cmd.source.id);
      ImGui::Text("Dims: %ux%ux%u", cmd.source.width, cmd.source.height, cmd.source.depth);
      ImGui::Text("Spacing: %.2f %.2f %.2f mm", cmd.source.spacing_mm.x, cmd.source.spacing_mm.y, cmd.source.spacing_mm.z);
      ImGui::Text("Window: %.2f / %.2f", cmd.display.window_center, cmd.display.window_width);
      ImGui::Text("Scalar: %s", cmd.source.format == render::VolumeScalarFormat::UInt16 ? "packed UInt16 (GPU window)" : "R8 normalized");
      ImGui::Text("WGSL pass: raygen.wgsl -> 3D texture + transfer LUT");
    }

    ImGui::SeparatorText("Volume display");
    world.view<render::VolumeRenderable>([&](ecs::Entity, render::VolumeRenderable& volume) {
      int mode = static_cast<int>(modeToGpu(volume.display.mode));
      if (ImGui::Combo("Mode", &mode, "DVR\0MIP\0Isosurface\0\0")) {
        volume.display.mode = mode == 1   ? render::VolumeRenderMode::MIP
                              : mode == 2 ? render::VolumeRenderMode::Isosurface
                                          : render::VolumeRenderMode::DVR;
      }

      int preset = static_cast<int>(volume.display.transfer_preset);
      if (ImGui::SliderInt("Transfer preset", &preset, 1, 4)) {
        volume.display.transfer_preset = static_cast<std::uint32_t>(preset);
      }

      if (ImGui::Button("Auto window from GPU histogram")) {
        runGpuHistogramAutoWindow(volume);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("compute pass + atomic bins");
      ImGui::TextWrapped("%s", histogramStatus.c_str());
      if (histogramAvailable) {
        ImGui::Text("p01 bin/value: %u / %.3f", histogramLowBin, histogramLowValue);
        ImGui::Text("p99 bin/value: %u / %.3f", histogramHighBin, histogramHighValue);
      }

      const bool packed = volume.source.format == render::VolumeScalarFormat::UInt16;
      if (packed) {
        ImGui::SliderFloat("Window center (stored)", &volume.display.window_center, 0.0f, 65535.0f);
        ImGui::SliderFloat("Window width (stored)", &volume.display.window_width, 1.0f, 65535.0f);
      } else {
        ImGui::SliderFloat("Window center", &volume.display.window_center, 0.0f, 1.0f);
        ImGui::SliderFloat("Window width", &volume.display.window_width, 0.05f, 2.0f);
      }

      int debug = static_cast<int>(debugMode);
      if (ImGui::Combo("Debug view", &debug, "Final\0Ray direction\0Ray depth\0Sample count\0\0")) {
        debugMode = static_cast<std::uint32_t>(glm::clamp(debug, 0, 3));
      }

      int steps = static_cast<int>(sampleSteps);
      if (ImGui::SliderInt("Ray samples", &steps, 16, 512)) {
        sampleSteps = static_cast<std::uint32_t>(glm::clamp(steps, 16, 512));
      }
      ImGui::SliderFloat("Opacity scale", &opacityScale, 0.01f, 1.0f);
    });
    ImGui::TextDisabled("Study knobs: debug exposes the ray math; samples trade quality for cost.");
    ImGui::End();
  }
  ImGui::Render();
  ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);

  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
  WGPUCommandBufferDescriptor cbd = {};
  WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cbd);
  wgpuQueueSubmit(queue, 1, &cb);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(enc);
#  ifndef __EMSCRIPTEN__
  wgpuSurfacePresent(surface);
#  endif
  wgpuTextureViewRelease(view);
  wgpuTextureRelease(st.texture);
}

void AppWebGPU::cleanup() {
  releaseVolumeRenderer();
  ImGui_ImplWGPU_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  if (queue) wgpuQueueRelease(queue);
  if (device) wgpuDeviceRelease(device);
  if (adapter) wgpuAdapterRelease(adapter);
  if (surface) {
    wgpuSurfaceUnconfigure(surface);
    wgpuSurfaceRelease(surface);
  }
  if (instance) wgpuInstanceRelease(instance);
#  if defined(SDL_PLATFORM_APPLE)
  if (metalView) SDL_Metal_DestroyView(metalView);
#  endif
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
}

#else
// ============================================================================
// G2: WebGL2 / OpenGL BUILD (immediate-mode, the learning rung)
// ============================================================================

#  ifdef __EMSCRIPTEN__
#    include <GLES3/gl3.h>
static constexpr const char* VERT_GLSL = "#version 300 es\n";
static constexpr const char* FRAG_GLSL = "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n";
#  else
#    include <GL/glew.h>
static constexpr const char* VERT_GLSL = "#version 330 core\n";
static constexpr const char* FRAG_GLSL = "#version 330 core\n";
#  endif

#  include <vector>
#  include "shader_program.h"
#  include "vertex_buffer.h"
#  include "vertex_array.h"
#  include "renderer.h"
#  include "texture.h"
#  include "texture3d.h"
#  include "framebuffer.h"
#  include "embedded_shaders.h"  // generated from apps/dicom_viewer/shaders/*.glsl by CMake

// --- Transfer function (driven by the 1-4 buttons) ---------------------------
// Selects how density maps to colour + opacity. g_tf_dirty triggers a LUT
// rebuild in the render loop.
static int g_tf_preset = 1;
static bool g_tf_dirty = true;
static int g_render_mode = 0;  // 0 = DVR, 1 = MIP, 2 = Isosurface
static float g_window_center = 0.5f;
static float g_window_width = 1.0f;
static glm::vec3 g_box_half(1.0f);     // volume box half-extents (from voxel spacing)
static int g_debug_mode = 0;           // 0 final, 1 ray dir, 2 depth, 3 samples
static int g_sample_steps = 128;       // ray-march samples; quality/cost knob
static float g_opacity_scale = 0.20f;  // per-sample opacity multiplier

// Build a 256-entry RGBA transfer LUT for the given preset.
static std::vector<unsigned char> make_transfer_lut(int preset) {
  std::vector<unsigned char> lut(256 * 4);
  for (int i = 0; i < 256; ++i) {
    float t = i / 255.0f;
    glm::vec3 rgb;
    float a;
    switch (preset) {
      case 2:  // "tissue": transparent low end, warm ramp
        a = glm::smoothstep(0.15f, 0.50f, t);
        rgb = glm::mix(glm::vec3(0.55f, 0.12f, 0.05f), glm::vec3(1.00f, 0.92f, 0.78f), t);
        break;
      case 3:  // "shell": only a narrow density band is opaque
        a = (t > 0.30f && t < 0.55f) ? 0.9f : 0.0f;
        rgb = glm::vec3(0.2f, 0.9f, 0.6f);
        break;
      case 4:  // "cool": blue -> cyan -> white
        a = t;
        rgb = glm::mix(glm::vec3(0.0f, 0.1f, 0.4f), glm::vec3(0.7f, 0.95f, 1.0f), t);
        break;
      default:  // 1 "gray": density as grayscale
        a = t;
        rgb = glm::vec3(t);
        break;
    }
    lut[i * 4 + 0] = static_cast<unsigned char>(glm::clamp(rgb.r, 0.0f, 1.0f) * 255.0f);
    lut[i * 4 + 1] = static_cast<unsigned char>(glm::clamp(rgb.g, 0.0f, 1.0f) * 255.0f);
    lut[i * 4 + 2] = static_cast<unsigned char>(glm::clamp(rgb.b, 0.0f, 1.0f) * 255.0f);
    lut[i * 4 + 3] = static_cast<unsigned char>(glm::clamp(a, 0.0f, 1.0f) * 255.0f);
  }
  return lut;
}

// Load a raw R8 volume of n^3 bytes from disk. Returns empty on any failure so
// the caller can fall back to the synthetic volume. This is the same load path
// real DICOM data will use (just different bytes) — the point of Tier 3 / path B.
static std::vector<unsigned char> load_volume_raw(const char* path, int n) {
  std::vector<unsigned char> data;
  FILE* f = fopen(path, "rb");
  if (!f) return data;
  const size_t expected = static_cast<size_t>(n) * n * n;
  data.resize(expected);
  const size_t got = fread(data.data(), 1, expected, f);
  fclose(f);
  if (got != expected) data.clear();
  return data;
}

// Synthetic fallback volume: a soft ball, density 1 at the centre falling to 0
// at the edge. Used when the raw file is missing.
static std::vector<unsigned char> make_volume(int n) {
  std::vector<unsigned char> v(static_cast<size_t>(n) * n * n);
  for (int z = 0; z < n; ++z) {
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        // voxel centre in [-1, 1]
        glm::vec3 c = (glm::vec3(x, y, z) / float(n - 1) - 0.5f) * 2.0f;
        float r = glm::length(c);
        float density = glm::clamp(1.0f - r / 0.9f, 0.0f, 1.0f);
        density *= density;  // softer falloff
        v[(static_cast<size_t>(z) * n + y) * n + x] = static_cast<unsigned char>(density * 255.0f);
      }
    }
  }
  return v;
}

struct AppWebGL {
  SDL_Window* window = nullptr;
  SDL_GLContext context = nullptr;
  // Pass 1 (volume ray cast -> FBO): fullscreen quad + 3D volume texture
  std::unique_ptr<engine::VertexArray> vao;
  std::unique_ptr<engine::VertexBuffer> vbo;
  std::unique_ptr<engine::ShaderProgram> shader;
  std::unique_ptr<engine::Texture3D> volume;
  std::unique_ptr<engine::Texture2D> transferLut;  // 256x1 density->RGBA

  // Pass 2 (FBO -> screen): fullscreen quad + post shader
  std::unique_ptr<engine::VertexArray> postVao;
  std::unique_ptr<engine::VertexBuffer> postVbo;
  std::unique_ptr<engine::ShaderProgram> postShader;
  std::unique_ptr<engine::Framebuffer> fbo;
  int fbW = 0, fbH = 0;

  engine::Renderer renderer;
  bool running = true;

  bool init();
  void tick();
  void cleanup();

private:
  bool compileShaders();
  bool compilePostShader();
  bool setupGeometry();
  bool setupPostGeometry();
  void ensureFramebuffer(int w, int h);
};

bool AppWebGL::compileShaders() {
  // Source from apps/dicom_viewer/shaders/raygen.glsl (embedded at build time). The same file
  // holds both stages; we compile it twice with VERTEX_SHADER / FRAGMENT_SHADER
  // defined. The #version / precision header is prepended here so one source
  // serves WebGL2/GLES3 and desktop GL 3.3.
  std::string vertSrc = std::string(VERT_GLSL) + "#define VERTEX_SHADER\n" + shaders::RAYGEN_GLSL;
  std::string fragSrc = std::string(FRAG_GLSL) + "#define FRAGMENT_SHADER\n" + shaders::RAYGEN_GLSL;

  std::string errmsg;
  auto next = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);
  if (!next->isValid()) {
    fprintf(stderr, "Shader compile failed: %s\n", errmsg.c_str());
    return false;
  }
  shader = std::move(next);
  renderer.setShaderProgram(shader.get());
  return true;
}

bool AppWebGL::setupGeometry() {
  // Fullscreen quad: pos in NDC (-1..1), uv = (pos + 1) / 2 so the fragment
  // shader can reconstruct clip-space xy as uv*2-1.
  const float verts[] = {
      // pos            uv
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,

      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
  };

  vbo = std::make_unique<engine::VertexBuffer>(verts, sizeof(verts));
  if (!vbo || vbo->getHandle() == 0) {
    fprintf(stderr, "Failed to create vertex buffer\n");
    return false;
  }
  vao = std::make_unique<engine::VertexArray>();
  if (!vao || vao->getHandle() == 0) {
    fprintf(stderr, "Failed to create vertex array\n");
    return false;
  }

  const GLsizei stride = 4 * sizeof(float);
  vao->bind();
  vbo->bind();
  vao->setVertexAttribute(0, 2, GL_FLOAT, 0, stride);                  // position
  vao->setVertexAttribute(1, 2, GL_FLOAT, 2 * sizeof(float), stride);  // uv
  engine::VertexArray::unbind();
  engine::VertexBuffer::unbind();

  // Load the volume from a raw file (preloaded into the WASM FS on the web, or
  // an absolute path natively). Fall back to the synthetic ball if missing.
  const int N = 96;
#  ifdef __EMSCRIPTEN__
  const char* volPath = "/volume.raw";
#  else
  const char* volPath = VOLUME_PATH;
#  endif
  std::vector<unsigned char> voxels = load_volume_raw(volPath, N);
  if (voxels.empty()) {
    fprintf(stderr, "volume.raw not found/invalid at %s — using synthetic\n", volPath);
    voxels = make_volume(N);
  } else {
    printf("Loaded volume %s (%d^3)\n", volPath, N);
  }
  volume = std::make_unique<engine::Texture3D>(N, N, N, voxels.data());
  if (!volume || volume->getHandle() == 0) {
    fprintf(stderr, "Failed to create volume texture\n");
    return false;
  }

  // Voxel spacing -> box half-extents. Real CT slices are thicker than pixels
  // are wide; we simulate that here (z = 1.5x) so the box reflects physical
  // proportions instead of squishing. With DICOM this comes from the file;
  // (1,1,1) would render a perfectly cubic box.
  const glm::vec3 spacing(1.0f, 1.0f, 1.5f);
  glm::vec3 phys = glm::vec3(static_cast<float>(N)) * spacing;
  g_box_half = phys / glm::max(phys.x, glm::max(phys.y, phys.z));
  return true;
}

// Pass 2 shader: blit the offscreen scene texture onto a fullscreen quad,
// multiplied by a tint. This is the "deliver the render-texture to the screen"
// step; the per-pixel work lives in pass 1.
bool AppWebGL::compilePostShader() {
  // Source from apps/dicom_viewer/shaders/blit.glsl (both stages, compiled twice).
  std::string vertSrc = std::string(VERT_GLSL) + "#define VERTEX_SHADER\n" + shaders::BLIT_GLSL;
  std::string fragSrc = std::string(FRAG_GLSL) + "#define FRAGMENT_SHADER\n" + shaders::BLIT_GLSL;

  std::string errmsg;
  auto next = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);
  if (!next->isValid()) {
    fprintf(stderr, "Post shader compile failed: %s\n", errmsg.c_str());
    return false;
  }
  postShader = std::move(next);
  return true;
}

bool AppWebGL::setupPostGeometry() {
  // Fullscreen quad in NDC (-1..1) with UVs (0..1). Same axis convention as
  // the FBO texture (y up), so the scene is displayed upright (no flip).
  const float verts[] = {
      // pos            uv
      -1.0f, 1.0f,  0.0f, 1.0f,  // top-left
      -1.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
      1.0f,  -1.0f, 1.0f, 0.0f,  // bottom-right

      -1.0f, 1.0f,  0.0f, 1.0f,  // top-left
      1.0f,  -1.0f, 1.0f, 0.0f,  // bottom-right
      1.0f,  1.0f,  1.0f, 1.0f,  // top-right
  };

  postVbo = std::make_unique<engine::VertexBuffer>(verts, sizeof(verts));
  if (!postVbo || postVbo->getHandle() == 0) {
    fprintf(stderr, "Failed to create post vertex buffer\n");
    return false;
  }
  postVao = std::make_unique<engine::VertexArray>();
  if (!postVao || postVao->getHandle() == 0) {
    fprintf(stderr, "Failed to create post vertex array\n");
    return false;
  }

  const GLsizei stride = 4 * sizeof(float);
  postVao->bind();
  postVbo->bind();
  postVao->setVertexAttribute(0, 2, GL_FLOAT, 0, stride);
  postVao->setVertexAttribute(1, 2, GL_FLOAT, 2 * sizeof(float), stride);
  engine::VertexArray::unbind();
  engine::VertexBuffer::unbind();
  return true;
}

// (Re)create the offscreen target when missing or when the drawable size changed.
void AppWebGL::ensureFramebuffer(int w, int h) {
  if (w <= 0 || h <= 0) return;
  if (fbo && fbW == w && fbH == h) return;
  fbo = std::make_unique<engine::Framebuffer>(w, h);
  fbW = w;
  fbH = h;
  if (!fbo->isComplete()) {
    fprintf(stderr, "Framebuffer incomplete at %dx%d\n", w, h);
  }
}

bool AppWebGL::init() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

#  ifdef __EMSCRIPTEN__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#  else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#  endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  window = SDL_CreateWindow("DICOM Renderer (WebGL2)", 800, 600, SDL_WINDOW_OPENGL);
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  context = SDL_GL_CreateContext(window);
  if (!context) {
    fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return false;
  }
  SDL_GL_SetSwapInterval(1);

#  ifndef __EMSCRIPTEN__
  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(err));
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return false;
  }
#  endif

  if (!compileShaders() || !setupGeometry() || !compilePostShader() || !setupPostGeometry()) {
    cleanup();
    return false;
  }

  renderer.setClearColor(0.1f, 0.2f, 0.5f, 1.0f);
  printf("WebGL2 (G2) initialized (render-to-texture pipeline).\n");
  return true;
}

void AppWebGL::tick() {
  process_input(running);
  g_camera.update(measure_delta_seconds());

  // Match the offscreen target + viewport to the CURRENT drawable size.
  int w = 0, h = 0;
  get_drawable_size(window, w, h);
  g_camera.set_viewport(w, h);  // keep aspect ratio in sync with the viewport
  ensureFramebuffer(w, h);

  // ---- PASS 1: generate one ray per pixel INTO the offscreen framebuffer ----
  // The camera reaches the shader as the inverse view-projection matrix.
  // Rebuild the transfer LUT if the preset changed.
  if (g_tf_dirty) {
    g_tf_dirty = false;
    std::vector<unsigned char> lut = make_transfer_lut(g_tf_preset);
    transferLut = std::make_unique<engine::Texture2D>(256, 1, lut.data());
  }

  if (fbo) fbo->bind();
  glViewport(0, 0, fbW, fbH);
  renderer.clear();
  if (shader) {
    shader->use();
    glm::mat4 invVP = glm::inverse(g_camera.get_view_projection());
    shader->setUniform("inv_view_projection", invVP);
    shader->setUniform("uVolume", 0);    // 3D volume on unit 0
    shader->setUniform("uTransfer", 1);  // transfer LUT on unit 1
    shader->setUniform("uMode", g_render_mode);
    shader->setUniform("uDebug", g_debug_mode);
    shader->setUniform("uSteps", g_sample_steps);
    shader->setUniform("uOpacityScale", g_opacity_scale);
    shader->setUniform("uWindow", glm::vec2(g_window_center, g_window_width));
    shader->setUniform("uBoxHalf", g_box_half);
  }
  if (volume) volume->bind(0);
  if (transferLut) transferLut->bind(1);
  if (vao) renderer.draw(*vao, 6);  // fullscreen quad -> volume ray cast

  // ---- PASS 2: blit the ray-gen texture to the screen (tinted) ----
  engine::Framebuffer::bindDefault();
  glViewport(0, 0, w, h);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (postShader && fbo && postVao) {
    postShader->use();
    postShader->setUniform("uScene", 0);
    postShader->setUniform("uTint", glm::vec4(1.0f));  // no tint; blit as-is
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbo->getColorTexture());
    postVao->bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    engine::VertexArray::unbind();
  }

  SDL_GL_SwapWindow(window);
}

void AppWebGL::cleanup() {
  fbo.reset();
  postShader.reset();
  postVao.reset();
  postVbo.reset();
  transferLut.reset();
  volume.reset();
  shader.reset();
  vao.reset();
  vbo.reset();
  if (context) SDL_GL_DestroyContext(context);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
}

#endif  // USE_WEBGPU

// ============================================================================
// EXPORTED C FUNCTIONS FOR JAVASCRIPT (Emscripten)
// ============================================================================
#ifdef __EMSCRIPTEN__
extern "C" {
// Called by the shell when the canvas is resized.
EMSCRIPTEN_KEEPALIVE
void on_canvas_resize(int width, int height) {
  // Only RECORD the size (+ camera aspect). The render loop applies
  // glViewport every frame from g_canvas_w/h. Calling GL here is unsafe:
  // the shell fires this from onRuntimeInitialized, which runs BEFORE
  // main() creates the GL context — glViewport would crash on a null
  // context. (WebGPU reconfigures its context in JS.)
  g_canvas_w = width;
  g_canvas_h = height;
  g_camera.set_viewport(width, height);
}

#  ifndef USE_WEBGPU
// Transfer-function preset (WebGL build only — WebGPU does it in JS).
EMSCRIPTEN_KEEPALIVE
void set_shader_variant(int variant_num) {
  if (variant_num >= 1 && variant_num <= 4) {
    g_tf_preset = variant_num;
    g_tf_dirty = true;
  }
}

// Render mode: 0 = DVR, 1 = MIP, 2 = Isosurface.
EMSCRIPTEN_KEEPALIVE
void set_render_mode(int mode) {
  if (mode >= 0 && mode <= 2) g_render_mode = mode;
}

// Window/level (center, width) in normalized [0,1] density.
EMSCRIPTEN_KEEPALIVE
void set_window(float center, float width) {
  g_window_center = center;
  g_window_width = (width < 0.01f) ? 0.01f : width;
}

// Debug view: 0 final image, 1 ray direction, 2 ray depth, 3 sample count.
EMSCRIPTEN_KEEPALIVE
void set_debug_mode(int mode) {
  if (mode < 0) mode = 0;
  if (mode > 3) mode = 3;
  g_debug_mode = mode;
}

// Sampling controls. More steps reduce banding but cost more fragment work.
EMSCRIPTEN_KEEPALIVE
void set_sampling(int steps, float opacity) {
  if (steps < 16) steps = 16;
  if (steps > 512) steps = 512;
  if (opacity < 0.01f) opacity = 0.01f;
  if (opacity > 1.0f) opacity = 1.0f;
  g_sample_steps = steps;
  g_opacity_scale = opacity;
}
#  endif
}
#endif

// ============================================================================
// MAIN
// ============================================================================

static const char* renderer_name() {
#ifdef USE_WEBGPU
  return "WebGPU (G3)";
#else
  return "WebGL2 (G2)";
#endif
}

#ifdef USE_WEBGPU
using App = AppWebGPU;
#else
using App = AppWebGL;
#endif

static App* g_app = nullptr;

#ifdef __EMSCRIPTEN__
static void em_tick() {
  if (g_app && g_app->running) {
    g_app->tick();
  } else {
    emscripten_cancel_main_loop();
  }
}
#endif

int main() {
  printf("====================================\n");
  printf("DICOM Renderer — %s build\n", renderer_name());
  printf("====================================\n");

  static App app;
  g_app = &app;

  if (!app.init()) {
    fprintf(stderr, "Failed to initialize %s\n", renderer_name());
    return 1;
  }

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop(em_tick, 0, 1);
#else
  while (app.running) {
    app.tick();
  }
  app.cleanup();
#endif
  return 0;
}
