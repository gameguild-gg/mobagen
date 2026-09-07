// ============================================================================
// DICOM Renderer — WebGPU volume rendering app on the core app host.
// ============================================================================
// The window, the WebGPU instance/adapter/device/queue/surface and the Dear
// ImGui SDL3+WebGPU backends are owned by the core app host (core/sources/app,
// core/sources/imgui); this file registers DicomApp via MOBAGEN_MAIN and keeps
// only the app-specific state: the camera, the DOD scene (Transform +
// VolumeRenderable -> RenderBridge) and the WGSL volume ray-cast pipeline
// (raygen.wgsl 3D-texture pass + histogram.wgsl compute auto-window).
//
// Historical note: this app once shipped a second, WebGL2/OpenGL build (the
// "learning rung" before WebGPU). A browser <canvas> can hold exactly ONE
// context for its lifetime, so the renderer was a compile-time selection; the
// WebGL2 path is long gone and USE_WEBGPU is forced by CMake.

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  include <emscripten/html5.h>
#endif

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"
#include "camera/camera.hpp"

// ============================================================================
// CAMERA & INPUT
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

// ============================================================================
// WEBGPU VOLUME RENDERER (app code; device/queue/surface come from the host)
// ============================================================================
#include <webgpu/webgpu.h>
#include <imgui.h>
#include "render_bridge.hpp"
#include "transform_system.hpp"
#include "volume_buffer.h"
#include "volume_file.h"
#include "embedded_shaders.h"
#ifdef HAVE_GDCM
#  include "volume_io.h"
#endif

static const char* renderer_name() { return "WebGPU (G3)"; }

namespace {

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

  void reportStartupStatus(const char* kind, const char* message) {
    if (std::strcmp(kind, "error") == 0) {
      SDL_Log("%s: %s", kind, message);
    } else {
      printf("[%s] %s\n", kind, message);
    }
#ifdef __EMSCRIPTEN__
    EM_ASM(
        {
          const kind = UTF8ToString($0);
          const message = UTF8ToString($1);
          if (globalThis.mobagenSetStatus) globalThis.mobagenSetStatus(kind, message);
        },
        kind, message);
#endif
  }

  // Pump WebGPU events until the callback fires. The instance is owned by the
  // host context (no accessor), so pump through its tick(): web runs
  // wgpuInstanceProcessEvents, native runs wgpuDeviceTick — both deliver
  // AllowProcessEvents callbacks (emdawnwebgpu only delivers them while
  // events are processed; SDL_Delay/emscripten_sleep yields to the browser).
  bool pumpUntil(app::WebGPUContext& gpu, bool& flag, const char* operation, Uint64 timeoutMs = 10000) {
    const Uint64 start = SDL_GetTicks();
    while (!flag) {
      gpu.tick();
      if (SDL_GetTicks() - start > timeoutMs) {
        SDL_Log("%s timed out after %llu ms", operation, static_cast<unsigned long long>(timeoutMs));
        return false;
      }
#ifdef __EMSCRIPTEN__
      emscripten_sleep(1);
#else
      SDL_Delay(1);
#endif
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
#ifdef HAVE_GDCM
#  ifdef MOBAGEN_DICOM_PATH
    const char* dicomDir = MOBAGEN_DICOM_PATH;
#  else
    const char* dicomDir = "apps/dicom_viewer/assets/dicom";
#  endif
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
#else
    return {};
#endif
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

// ============================================================================
// DicomApp — AppCallbacks for the core host; owns the volume renderer state
// ============================================================================
struct DicomApp : app::AppCallbacks {
  app::ImGuiLayer imguiLayer;

  // App scene state (the ecs::World itself is host-owned: app.world).
  scene::TransformSystem transforms;
  render::RenderBridge renderBridge;
  volume::VolumeBuffer cpuVolume;
  bool cpuVolumeFromDicom = false;

  // Volume pass resources (created lazily once the host device exists).
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

  // Migration bookkeeping: the volume renderer needs the host device, which
  // does not exist yet when on_init runs — init lazily on the first iterate.
  bool renderer_ready = false;
  int smoke_frames = -1;  // --smoke-frames N: exit(0) after N frames

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override;
  SDL_AppResult on_event(app::App& app, const SDL_Event& event) override;
  SDL_AppResult on_iterate(app::App& app, float dt) override;
  void on_draw(app::App& app, WGPURenderPassEncoder pass) override;
  void on_shutdown(app::App& app) override;

private:
  void createStudyVolumeScene(app::App& app);
  bool initVolumeRenderer(app::App& app);
  bool initHistogramResources(app::App& app, const render::VolumeSource& source);
  bool runGpuHistogramAutoWindow(app::App& app, render::VolumeRenderable& volume);
  void uploadTransferLut(app::App& app, std::uint32_t preset);
  void drawVolume(app::App& app, WGPURenderPassEncoder pass);
  void releaseVolumeRenderer();
};

void DicomApp::createStudyVolumeScene(app::App& app) {
  // This is the first live DOD -> renderer handoff:
  //   Entity + Transform + VolumeRenderable
  // becomes, every frame:
  //   VolumeDrawCommand[] consumed by the renderer host.
  //
  // The WebGPU host records a WGSL volume pass from RenderBridge commands,
  // then draws ImGui as an overlay. The important step here is architectural:
  // the renderer no longer needs to query ECS storage while recording GPU
  // commands. It receives a flat command list.
  cpuVolume = tryLoadDicomVolumeBuffer(cpuVolumeFromDicom);
  if (cpuVolume.empty()) {
    // Web (and native without GDCM): load the offline-converted DICOM volume.
    // scripts/dicom_to_mvol.py turns the series into apps/dicom_viewer/assets/volume.mvol (packed
    // UInt16 RG8 + metadata); the wasm build preloads it into the FS. This is
    // what brings REAL DICOM intensities (GPU window/level + histogram) to the
    // browser, where GDCM is unavailable.
#ifdef __EMSCRIPTEN__
    const char* mvolPath = "/volume.mvol";
#elif defined(MOBAGEN_MVOL_PATH)
    const char* mvolPath = MOBAGEN_MVOL_PATH;
#else
    const char* mvolPath = "apps/dicom_viewer/assets/volume.mvol";
#endif
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

  ecs::Entity phantom = app.world.create();

  scene::Transform t;
  t.scale = {1.0f, 1.0f, 1.0f};
  app.world.add<scene::Transform>(phantom, t);

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
  app.world.add<render::VolumeRenderable>(phantom, volume);

  transforms.rebuild(app.world);
}

bool DicomApp::initVolumeRenderer(app::App& app) {
  WGPUDevice device = app.device();
  WGPUQueue queue = app.gpu.queue();
  // The volume pass renders into the host frame target: the window surface
  // format, or the offscreen BGRA8Unorm target in HeadlessNull (the host
  // surface format is Undefined without a surface — same fallback the core
  // ImGuiLayer applies).
  const WGPUTextureFormat targetFormat
      = app.gpu.surface_format() != WGPUTextureFormat_Undefined ? app.gpu.surface_format() : WGPUTextureFormat_BGRA8Unorm;

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

      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
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
  uploadTransferLut(app, 1u);

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
  colorTarget.format = targetFormat;
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

  if (!initHistogramResources(app, source)) {
    fprintf(stderr, "Failed to create GPU histogram resources\n");
    return false;
  }

  printf("WebGPU volume renderer initialized (%ux%ux%u %s, %s)\n", source.width, source.height, source.depth,
         cpuVolumeFromDicom ? "DICOM" : "phantom", packedU16 ? "packed UInt16 RG8" : "R8 normalized");
  return true;
}

bool DicomApp::initHistogramResources(app::App& app, const render::VolumeSource& source) {
  WGPUDevice device = app.device();
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

bool DicomApp::runGpuHistogramAutoWindow(app::App& app, render::VolumeRenderable& volume) {
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
  wgpuQueueWriteBuffer(app.gpu.queue(), histogramParamsBuffer, 0, &params, sizeof(params));

  WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encDesc.label = {"histogram command encoder", WGPU_STRLEN};
  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(app.device(), &encDesc);
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
  wgpuQueueSubmit(app.gpu.queue(), 1, &cb);
  wgpuCommandBufferRelease(cb);
  wgpuCommandEncoderRelease(enc);

  MapReq mapReq;
  WGPUBufferMapCallbackInfo mapCb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
  mapCb.mode = WGPUCallbackMode_AllowProcessEvents;
  mapCb.callback = onBufferMapped;
  mapCb.userdata1 = &mapReq;
  wgpuBufferMapAsync(histogramReadbackBuffer, WGPUMapMode_Read, 0, static_cast<size_t>(histogramBytes), mapCb);
  if (!pumpUntil(app.gpu, mapReq.done, "histogramReadback", 30000)) {
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

void DicomApp::uploadTransferLut(app::App& app, std::uint32_t preset) {
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
  wgpuQueueWriteTexture(app.gpu.queue(), &dst, lut.data(), lut.size(), &layout, &writeSize);
  uploadedTransferPreset = preset;
}

void DicomApp::drawVolume(app::App& app, WGPURenderPassEncoder pass) {
  const auto& commands = renderBridge.volume_commands();
  if (commands.empty() || !volumePipeline || !volumeBindGroup) return;

  const render::VolumeDrawCommand& cmd = commands[0];
  if (cmd.display.transfer_preset != uploadedTransferPreset) {
    uploadTransferLut(app, cmd.display.transfer_preset);
  }

  const glm::mat4 invVP = glm::inverse(g_camera.get_view_projection());
  const GpuVec4u mode = {modeToGpu(cmd.display.mode), debugMode, sampleSteps, scalarFormatToGpu(cmd.source.format)};
  const GpuVec4f windowLevel = {cmd.display.window_center, glm::max(cmd.display.window_width, 0.001f), cmd.display.iso_threshold, opacityScale};
  const glm::vec3 half = boxHalfFromSource(cmd.source);
  const GpuVec4f boxHalf = {half.x, half.y, half.z, 0.0f};

  WGPUQueue queue = app.gpu.queue();
  wgpuQueueWriteBuffer(queue, cameraBuffer, 0, glm::value_ptr(invVP), sizeof(glm::mat4));
  wgpuQueueWriteBuffer(queue, modeBuffer, 0, &mode, sizeof(mode));
  wgpuQueueWriteBuffer(queue, windowBuffer, 0, &windowLevel, sizeof(windowLevel));
  wgpuQueueWriteBuffer(queue, boxHalfBuffer, 0, &boxHalf, sizeof(boxHalf));

  wgpuRenderPassEncoderSetPipeline(pass, volumePipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, volumeBindGroup, 0, nullptr);
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, fullscreenVbo, 0, 6u * 4u * sizeof(float));
  wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
}

void DicomApp::releaseVolumeRenderer() {
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

// ============================================================================
// Host callbacks
// ============================================================================
SDL_AppResult DicomApp::on_init(app::App& app, int argc, char** argv) {
  app.settings.title = "DICOM Renderer (WebGPU)";
  app.settings.width = 800;
  app.settings.height = 600;
  app.settings.high_pixel_density = true;
  app.settings.clear_color[0] = 0.10f;
  app.settings.clear_color[1] = 0.20f;
  app.settings.clear_color[2] = 0.50f;
  app.settings.clear_color[3] = 1.0f;

  app::AppSettings::parse(argc, argv, app.settings);
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--smoke-frames") == 0 && i + 1 < argc) {
      smoke_frames = std::atoi(argv[++i]);
    }
  }
#ifdef __EMSCRIPTEN__
  // The host window maps onto the HTML canvas; start it at the shell's CSS
  // layout size instead of the default 800x600 (the shell owns the layout).
  double cw = 0, ch = 0;
  if (emscripten_get_element_css_size("#canvas", &cw, &ch) == EMSCRIPTEN_RESULT_SUCCESS && cw > 0 && ch > 0) {
    app.settings.width = (int)cw;
    app.settings.height = (int)ch;
  }
#endif

  // HeadlessNone has no device, so the ImGui layer can never init — the host
  // would turn that into a startup failure. Attach only when a GPU frame will
  // exist; on_draw never runs headless, so no GUI is lost.
  if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) {
    app.attach_gui(imguiLayer);
  }

  printf("====================================\n");
  printf("DICOM Renderer — %s build\n", renderer_name());
  printf("====================================\n");

  // CPU-side scene setup: volume load (DICOM/.mvol/phantom) + DOD scene +
  // first RenderBridge build. The GPU half (initVolumeRenderer) waits for the
  // host device on the first iterate.
  createStudyVolumeScene(app);
  renderBridge.build(app.world);
  return SDL_APP_CONTINUE;
}

SDL_AppResult DicomApp::on_event(app::App& app, const SDL_Event& event) {
  // ImGui capture state (no context in HeadlessNone — keys go to the camera).
  ImGuiIO* io = ImGui::GetCurrentContext() != nullptr ? &ImGui::GetIO() : nullptr;
  switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
      if (!(io && io->WantCaptureKeyboard)) {
        handle_camera_key_down(event.key.key);
      }
      break;

    case SDL_EVENT_KEY_UP:
      if (!(io && io->WantCaptureKeyboard)) handle_camera_key_up(event.key.key);
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (!(io && io->WantCaptureMouse)) {
        g_mouse_look_active = true;
        g_mouse_drag_action
            = (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) ? MouseDragAction::Pan : MouseDragAction::Rotate;
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      g_mouse_look_active = false;
      g_mouse_drag_action = MouseDragAction::None;
      break;

    case SDL_EVENT_MOUSE_MOTION:
      if (g_mouse_look_active && !(io && io->WantCaptureMouse)) {
        if (g_mouse_drag_action == MouseDragAction::Pan) {
          g_camera.on_mouse_pan(event.motion.xrel, event.motion.yrel);
        } else {
          g_camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
        }
      }
      break;

    case SDL_EVENT_MOUSE_WHEEL:
      if (!(io && io->WantCaptureMouse)) g_camera.on_mouse_wheel(event.wheel.y);
      break;

    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      // The host already reconfigured the surface (deduped); keep the camera
      // aspect in sync with the configured size.
      g_camera.set_viewport(app.width(), app.height());
      break;

    default:
      break;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult DicomApp::on_iterate(app::App& app, float dt) {
  // Lazy device-dependent init (on_init runs before the host creates the
  // device): first iterate is the earliest point with device+queue ready.
  if (!renderer_ready && app.device() != nullptr) {
    renderer_ready = true;
    g_camera.set_viewport(app.width(), app.height());
    if (!initVolumeRenderer(app)) {
      reportStartupStatus("error", "WebGPU started, but the volume renderer failed to initialize.");
      fprintf(stderr, "WebGPU volume renderer init failed\n");
      return SDL_APP_FAILURE;  // old init() returned false -> exit 1
    }
    reportStartupStatus("ready", "WebGPU renderer ready.");
    printf("WebGPU (G3) initialized — Dawn + ImGui (surfaceFormat=%d)\n", (int)app.gpu.surface_format());
  }

  g_camera.update(dt);
  transforms.update(app.world);
  renderBridge.build(app.world);

  if (smoke_frames > 0) {
    --smoke_frames;
    if (smoke_frames == 0) {
      app.request_exit();
    }
  }
  return SDL_APP_CONTINUE;
}

void DicomApp::on_draw(app::App& app, WGPURenderPassEncoder pass) {
  // The old app enabled docking alongside the nav flags the core ImGuiLayer
  // sets; the layer inits after on_init, so flip the flag on the first frame.
  static const bool docking_enabled = [] {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    return true;
  }();
  (void)docking_enabled;

  // Scene pass: consume RenderBridge commands with the WGSL volume pipeline.
  // ImGui is drawn afterwards, so the controls remain a normal overlay.
  drawVolume(app, pass);

  // ImGui frame (drawn into the same render pass, after any scene geometry).
  ImGuiIO& io = ImGui::GetIO();
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
  ImGui::ColorEdit3("Clear color", app.settings.clear_color);
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
  app.world.view<render::VolumeRenderable>([&](ecs::Entity, render::VolumeRenderable& volume) {
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
      runGpuHistogramAutoWindow(app, volume);
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

void DicomApp::on_shutdown(app::App& app) {
  (void)app;
  // Volume resources die before the host tears down gui/device/surface
  // (host quit order: on_shutdown -> gui -> gpu).
  releaseVolumeRenderer();
}

// ============================================================================
// EXPORTED C FUNCTIONS FOR JAVASCRIPT (Emscripten)
// ============================================================================
#ifdef __EMSCRIPTEN__
extern "C" {
// Called by the shell when the canvas is resized.
EMSCRIPTEN_KEEPALIVE
void on_canvas_resize(int width, int height) {
  // Only RECORD the size (+ camera aspect). The render loop applies the
  // viewport from the configured sizes. Calling into WebGPU/SDL here is
  // unsafe: the shell fires this from onRuntimeInitialized, which runs BEFORE
  // the host creates the window/context.
  g_canvas_w = width;
  g_canvas_h = height;
  g_camera.set_viewport(width, height);
}
}
#endif

MOBAGEN_MAIN(DicomApp)
