// Prevent SDL from redefining main() to SDL_main()
// We want to control the entry point ourselves
#define SDL_MAIN_HANDLED

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera.h"

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

// Poll SDL events: quit, keyboard, mouse. Updates the shared camera.
// Works identically in native and Emscripten (SDL2 abstracts the event source).
static void process_input(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                g_camera.on_key_pressed(event.key.key);
                // 'C' toggles between ORBIT (DICOM viewing) and WASD (engine fly).
                if (event.key.key == SDLK_C) {
                    engine::CameraMode next =
                        (g_camera.get_mode() == engine::CameraMode::ORBIT)
                            ? engine::CameraMode::WASD
                            : engine::CameraMode::ORBIT;
                    g_camera.set_mode(next);
                    printf("Camera mode: %s\n",
                           next == engine::CameraMode::ORBIT ? "ORBIT" : "WASD");
                }
                break;

            case SDL_EVENT_KEY_UP:
                g_camera.on_key_released(event.key.key);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                g_mouse_look_active = true;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                g_mouse_look_active = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (g_mouse_look_active) {
                    g_camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
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

#include <webgpu/webgpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include "render_bridge.hpp"
#include "transform_system.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif
#if defined(SDL_PLATFORM_APPLE)
#include <SDL3/SDL_metal.h>
#endif

namespace {

// Dawn's wgpuInstanceRequestAdapter / RequestDevice are async even on native.
// Pump events until the callback fires (emscripten_sleep yields to JS on web).
struct AdapterReq { WGPUAdapter adapter = nullptr; bool done = false; };
void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
               WGPUStringView msg, void* ud1, void*) {
    auto* r = static_cast<AdapterReq*>(ud1);
    if (status == WGPURequestAdapterStatus_Success) r->adapter = adapter;
    else SDL_Log("RequestAdapter failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
    r->done = true;
}
struct DeviceReq { WGPUDevice device = nullptr; bool done = false; };
void onDevice(WGPURequestDeviceStatus status, WGPUDevice device,
              WGPUStringView msg, void* ud1, void*) {
    auto* r = static_cast<DeviceReq*>(ud1);
    if (status == WGPURequestDeviceStatus_Success) r->device = device;
    else SDL_Log("RequestDevice failed: %.*s", (int)msg.length, msg.data ? msg.data : "");
    r->done = true;
}
void onUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
    SDL_Log("[WGPU error type=%d]: %.*s", (int)type, (int)msg.length, msg.data ? msg.data : "");
}
void pumpUntil(WGPUInstance inst, bool& flag) {
    while (!flag) {
#ifdef __EMSCRIPTEN__
        emscripten_sleep(1);
#else
        wgpuInstanceProcessEvents(inst);
#endif
    }
}

}  // namespace

struct AppWebGPU {
    SDL_Window* window = nullptr;
    bool running = true;

    WGPUInstance      instance      = nullptr;
    WGPUAdapter       adapter       = nullptr;
    WGPUDevice        device        = nullptr;
    WGPUQueue         queue         = nullptr;
    WGPUSurface       surface       = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
#if defined(SDL_PLATFORM_APPLE)
    SDL_MetalView     metalView     = nullptr;
#endif
    int   cfgW = 0, cfgH = 0;
    float clearColor[4] = {0.10f, 0.20f, 0.50f, 1.0f};

    ecs::World world;
    scene::TransformSystem transforms;
    render::RenderBridge renderBridge;

    bool init();
    void tick();
    void cleanup();

private:
    void createSurface();
    bool initDeviceAndQueue();
    void configureSurface(int w, int h);
    void createStudyVolumeScene();
};

void AppWebGPU::createSurface() {
    WGPUSurfaceDescriptor desc = {};
#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector    = {"#canvas", WGPU_STRLEN};
    desc.nextInChain       = &canvasDesc.chain;
    surface = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_WIN32)
    WGPUSurfaceSourceWindowsHWND hwndDesc = {};
    hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwndDesc.hinstance   = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                               SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    hwndDesc.hwnd        = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    desc.nextInChain     = &hwndDesc.chain;
    surface = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_APPLE)
    metalView = SDL_Metal_CreateView(window);
    WGPUSurfaceSourceMetalLayer metalDesc = {};
    metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metalDesc.layer       = SDL_Metal_GetLayer(metalView);
    desc.nextInChain      = &metalDesc.chain;
    surface = wgpuInstanceCreateSurface(instance, &desc);
#elif defined(SDL_PLATFORM_LINUX)
    void* xdisplay = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                         SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    uint64_t xwindow = (uint64_t)SDL_GetNumberProperty(SDL_GetWindowProperties(window),
                         SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    WGPUSurfaceSourceXlibWindow xlibDesc = {};
    xlibDesc.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
    xlibDesc.display     = xdisplay;
    xlibDesc.window      = xwindow;
    desc.nextInChain     = &xlibDesc.chain;
    surface = wgpuInstanceCreateSurface(instance, &desc);
#else
#  error "Unsupported platform for WebGPU surface creation"
#endif
    if (!surface) fprintf(stderr, "wgpuInstanceCreateSurface failed\n");
}

bool AppWebGPU::initDeviceAndQueue() {
    AdapterReq aReq;
    WGPURequestAdapterOptions aOpts = {};
    aOpts.compatibleSurface = surface;
    aOpts.powerPreference   = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo aCb = {};
    aCb.mode      = WGPUCallbackMode_AllowProcessEvents;
    aCb.callback  = onAdapter;
    aCb.userdata1 = &aReq;
    wgpuInstanceRequestAdapter(instance, &aOpts, aCb);
    pumpUntil(instance, aReq.done);
    if (!aReq.adapter) { fprintf(stderr, "No WebGPU adapter\n"); return false; }
    adapter = aReq.adapter;

    DeviceReq dReq;
    WGPUDeviceDescriptor dDesc = {};
    dDesc.label = {"dicom_renderer device", WGPU_STRLEN};
    dDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    WGPURequestDeviceCallbackInfo dCb = {};
    dCb.mode      = WGPUCallbackMode_AllowProcessEvents;
    dCb.callback  = onDevice;
    dCb.userdata1 = &dReq;
    wgpuAdapterRequestDevice(adapter, &dDesc, dCb);
    pumpUntil(instance, dReq.done);
    if (!dReq.device) { fprintf(stderr, "WebGPU device request failed\n"); return false; }
    device = dReq.device;
    queue  = wgpuDeviceGetQueue(device);

    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface, adapter, &caps);
    surfaceFormat = (caps.formatCount > 0 && caps.formats) ? caps.formats[0]
                                                           : WGPUTextureFormat_BGRA8Unorm;
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    SDL_Log("WebGPU device ready (surfaceFormat=%d)", (int)surfaceFormat);
    return true;
}

void AppWebGPU::configureSurface(int w, int h) {
    if (w <= 0 || h <= 0) return;
    WGPUSurfaceConfiguration cfg = {};
    cfg.device      = device;
    cfg.format      = surfaceFormat;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
    cfg.width       = (uint32_t)w;
    cfg.height      = (uint32_t)h;
    cfg.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface, &cfg);
    cfgW = w; cfgH = h;
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
    ecs::Entity phantom = world.create();

    scene::Transform t;
    t.scale = {1.0f, 1.0f, 1.5f};  // demonstrate non-cubic voxel spacing
    world.add<scene::Transform>(phantom, t);

    render::VolumeRenderable volume;
    volume.source.id = 1;
    volume.source.width = 96;
    volume.source.height = 96;
    volume.source.depth = 96;
    volume.source.spacing_mm = {1.0f, 1.0f, 1.5f};
    volume.source.format = render::VolumeScalarFormat::UInt8;
    volume.display.window_center = 0.5f;
    volume.display.window_width = 1.0f;
    volume.display.transfer_preset = 1;
    volume.display.mode = render::VolumeRenderMode::DVR;
    world.add<render::VolumeRenderable>(phantom, volume);

    transforms.rebuild(world);
}

bool AppWebGPU::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    int w = 800, h = 600;
#ifdef __EMSCRIPTEN__
    double cw = 0, ch = 0;
    if (emscripten_get_element_css_size("#canvas", &cw, &ch) == EMSCRIPTEN_RESULT_SUCCESS
        && cw > 0 && ch > 0) { w = (int)cw; h = (int)ch; }
#endif
    window = SDL_CreateWindow("DICOM Renderer (WebGPU)", w, h,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    if (!instance) { fprintf(stderr, "wgpuCreateInstance failed\n"); return false; }

    createSurface();
    if (!surface) return false;
    if (!initDeviceAndQueue()) return false;

    int pxW = 0, pxH = 0;
    SDL_GetWindowSizeInPixels(window, &pxW, &pxH);
    configureSurface(pxW, pxH);
    g_camera.set_viewport(pxW > 0 ? pxW : w, pxH > 0 ? pxH : h);

    ImGui_ImplSDL3_InitForOther(window);
    ImGui_ImplWGPU_InitInfo wgpuInit = {};
    wgpuInit.Device             = device;
    wgpuInit.NumFramesInFlight  = 3;
    wgpuInit.RenderTargetFormat = surfaceFormat;
    wgpuInit.DepthStencilFormat = WGPUTextureFormat_Undefined;
    if (!ImGui_ImplWGPU_Init(&wgpuInit)) {
        fprintf(stderr, "ImGui_ImplWGPU_Init failed\n");
        return false;
    }

    createStudyVolumeScene();

    printf("WebGPU (G3) initialized — Dawn + ImGui (surfaceFormat=%d)\n", (int)surfaceFormat);
    return true;
}

void AppWebGPU::tick() {
    ImGuiIO& io = ImGui::GetIO();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) running = false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(window)) running = false;
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (!io.WantCaptureKeyboard) {
                    g_camera.on_key_pressed(event.key.key);
                    if (event.key.key == SDLK_C) {
                        engine::CameraMode next =
                            (g_camera.get_mode() == engine::CameraMode::ORBIT)
                                ? engine::CameraMode::WASD : engine::CameraMode::ORBIT;
                        g_camera.set_mode(next);
                    }
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (!io.WantCaptureKeyboard) g_camera.on_key_released(event.key.key);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!io.WantCaptureMouse) g_mouse_look_active = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                g_mouse_look_active = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (g_mouse_look_active && !io.WantCaptureMouse)
                    g_camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
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
    SDL_GetWindowSizeInPixels(window, &pxW, &pxH);
    if (pxW > 0 && pxH > 0 && (pxW != cfgW || pxH != cfgH)) {
        configureSurface(pxW, pxH);
        g_camera.set_viewport(pxW, pxH);
    }

    WGPUSurfaceTexture st = {};
    wgpuSurfaceGetCurrentTexture(surface, &st);
    bool ok = (st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
               st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) && st.texture;
    if (!ok) { if (st.texture) wgpuTextureRelease(st.texture); return; }

    WGPUTextureViewDescriptor vd = {};
    vd.format          = wgpuTextureGetFormat(st.texture);
    vd.dimension       = WGPUTextureViewDimension_2D;
    vd.mipLevelCount   = 1;
    vd.arrayLayerCount = 1;
    vd.aspect          = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, &vd);

    WGPUCommandEncoderDescriptor edesc = {};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &edesc);

    WGPURenderPassColorAttachment color = {};
    color.view       = view;
    color.loadOp     = WGPULoadOp_Clear;
    color.storeOp    = WGPUStoreOp_Store;
    color.clearValue = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]};
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pd = {};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments     = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pd);

    // ImGui frame (drawn into the same render pass, after any scene geometry).
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    {
        ImGui::Begin("DICOM Renderer — WebGPU (Dawn)");
        ImGui::Text("Dawn + ImGui live — %.1f FPS", io.Framerate);
        ImGui::Text("Camera: %s  (press C to toggle)",
                    g_camera.get_mode() == engine::CameraMode::ORBIT ? "ORBIT" : "WASD");
        ImGui::ColorEdit3("Clear color", clearColor);
        ImGui::SeparatorText("DOD render bridge");
        const auto& volumeCommands = renderBridge.volume_commands();
        ImGui::Text("Volume commands: %d", static_cast<int>(volumeCommands.size()));
        if (!volumeCommands.empty()) {
            const auto& cmd = volumeCommands[0];
            ImGui::Text("Volume id: %u", cmd.source.id);
            ImGui::Text("Dims: %ux%ux%u",
                        cmd.source.width, cmd.source.height, cmd.source.depth);
            ImGui::Text("Spacing: %.2f %.2f %.2f mm",
                        cmd.source.spacing_mm.x,
                        cmd.source.spacing_mm.y,
                        cmd.source.spacing_mm.z);
            ImGui::Text("Window: %.2f / %.2f",
                        cmd.display.window_center, cmd.display.window_width);
        }
        ImGui::TextDisabled("Next: consume this command with a Dawn WGSL volume pass.");
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
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(surface);
#endif
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);
}

void AppWebGPU::cleanup() {
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (queue)    wgpuQueueRelease(queue);
    if (device)   wgpuDeviceRelease(device);
    if (adapter)  wgpuAdapterRelease(adapter);
    if (surface)  { wgpuSurfaceUnconfigure(surface); wgpuSurfaceRelease(surface); }
    if (instance) wgpuInstanceRelease(instance);
#if defined(SDL_PLATFORM_APPLE)
    if (metalView) SDL_Metal_DestroyView(metalView);
#endif
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

#else
// ============================================================================
// G2: WebGL2 / OpenGL BUILD (immediate-mode, the learning rung)
// ============================================================================

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
static constexpr const char* VERT_GLSL = "#version 300 es\n";
static constexpr const char* FRAG_GLSL =
    "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n";
#else
#include <GL/glew.h>
static constexpr const char* VERT_GLSL = "#version 330 core\n";
static constexpr const char* FRAG_GLSL = "#version 330 core\n";
#endif

#include <vector>
#include "engine/shader_program.h"
#include "engine/vertex_buffer.h"
#include "engine/vertex_array.h"
#include "engine/renderer.h"
#include "engine/texture.h"
#include "engine/texture3d.h"
#include "engine/framebuffer.h"
#include "embedded_shaders.h"   // generated from shaders/*.glsl by CMake

// --- Transfer function (driven by the 1-4 buttons) ---------------------------
// Selects how density maps to colour + opacity. g_tf_dirty triggers a LUT
// rebuild in the render loop.
static int  g_tf_preset = 1;
static bool g_tf_dirty  = true;
static int  g_render_mode = 0;   // 0 = DVR, 1 = MIP, 2 = Isosurface
static float g_window_center = 0.5f;
static float g_window_width  = 1.0f;
static glm::vec3 g_box_half(1.0f);   // volume box half-extents (from voxel spacing)

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
                rgb = glm::mix(glm::vec3(0.55f, 0.12f, 0.05f),
                               glm::vec3(1.00f, 0.92f, 0.78f), t);
                break;
            case 3:  // "shell": only a narrow density band is opaque
                a = (t > 0.30f && t < 0.55f) ? 0.9f : 0.0f;
                rgb = glm::vec3(0.2f, 0.9f, 0.6f);
                break;
            case 4:  // "cool": blue -> cyan -> white
                a = t;
                rgb = glm::mix(glm::vec3(0.0f, 0.1f, 0.4f),
                               glm::vec3(0.7f, 0.95f, 1.0f), t);
                break;
            default: // 1 "gray": density as grayscale
                a = t;
                rgb = glm::vec3(t);
                break;
        }
        lut[i * 4 + 0] = static_cast<unsigned char>(glm::clamp(rgb.r, 0.0f, 1.0f) * 255.0f);
        lut[i * 4 + 1] = static_cast<unsigned char>(glm::clamp(rgb.g, 0.0f, 1.0f) * 255.0f);
        lut[i * 4 + 2] = static_cast<unsigned char>(glm::clamp(rgb.b, 0.0f, 1.0f) * 255.0f);
        lut[i * 4 + 3] = static_cast<unsigned char>(glm::clamp(a,     0.0f, 1.0f) * 255.0f);
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
                density *= density;                       // softer falloff
                v[(static_cast<size_t>(z) * n + y) * n + x] =
                    static_cast<unsigned char>(density * 255.0f);
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
    std::unique_ptr<engine::Texture2D> transferLut;   // 256x1 density->RGBA

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
    // Source from shaders/raygen.glsl (embedded at build time). The same file
    // holds both stages; we compile it twice with VERTEX_SHADER / FRAGMENT_SHADER
    // defined. The #version / precision header is prepended here so one source
    // serves WebGL2/GLES3 and desktop GL 3.3.
    std::string vertSrc = std::string(VERT_GLSL) + "#define VERTEX_SHADER\n"   + shaders::RAYGEN_GLSL;
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
        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f, -1.0f,    1.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,

        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,
        -1.0f,  1.0f,    0.0f, 1.0f,
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
#ifdef __EMSCRIPTEN__
    const char* volPath = "/volume.raw";
#else
    const char* volPath = VOLUME_PATH;
#endif
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
    // Source from shaders/blit.glsl (both stages, compiled twice).
    std::string vertSrc = std::string(VERT_GLSL) + "#define VERTEX_SHADER\n"   + shaders::BLIT_GLSL;
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
        -1.0f,  1.0f,    0.0f, 1.0f,   // top-left
        -1.0f, -1.0f,    0.0f, 0.0f,   // bottom-left
         1.0f, -1.0f,    1.0f, 0.0f,   // bottom-right

        -1.0f,  1.0f,    0.0f, 1.0f,   // top-left
         1.0f, -1.0f,    1.0f, 0.0f,   // bottom-right
         1.0f,  1.0f,    1.0f, 1.0f,   // top-right
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

#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window = SDL_CreateWindow("DICOM Renderer (WebGL2)",
                              800, 600, SDL_WINDOW_OPENGL);
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

#ifndef __EMSCRIPTEN__
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(err));
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
#endif

    if (!compileShaders() || !setupGeometry() ||
        !compilePostShader() || !setupPostGeometry()) {
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

    // Match the offscreen target + viewport to the CURRENT canvas size.
    // On the web the shell sets the canvas drawing buffer and tells us its size
    // via on_canvas_resize (g_canvas_w/h); SDL's window size is stale there.
    int w = 0, h = 0;
#ifdef __EMSCRIPTEN__
    w = g_canvas_w;
    h = g_canvas_h;
#else
    SDL_GetWindowSizeInPixels(window, &w, &h);
#endif
    if (w <= 0 || h <= 0) { w = 800; h = 600; }
    g_camera.set_viewport(w, h);   // keep aspect ratio in sync with the viewport
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
        shader->setUniform("uVolume", 0);     // 3D volume on unit 0
        shader->setUniform("uTransfer", 1);   // transfer LUT on unit 1
        shader->setUniform("uMode", g_render_mode);
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
    if (window)  SDL_DestroyWindow(window);
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

#ifndef USE_WEBGPU
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
#endif
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
