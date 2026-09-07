// ============================================================================
// App — host state + SDL3 callback lifecycle drivers. See app.hpp for the
// contract; sdl_app.cpp holds only the extern "C" SDL_App* trampolines.
// ============================================================================
// The frame flow is an extraction of the proven family-A main loop
// (apps/chess/main.cpp): clamped dt -> app update -> acquire -> status checks
// -> render pass with settings clear color -> submit -> present -> tick. The
// host never touches a GUI toolkit; GuiLayerView hooks carry the GUI.
#include "app/app.hpp"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include <cstdint>

namespace app {

  // ---------------------------------------------------------------------------
  // Registration
  // ---------------------------------------------------------------------------
  namespace {
    App* g_app = nullptr;
  }

  void set_app(App* app) { g_app = app; }
  App* app_instance() { return g_app; }

  // ---------------------------------------------------------------------------
  // App members
  // ---------------------------------------------------------------------------
  void App::request_exit(bool success) { pending_exit_ = success ? SDL_APP_SUCCESS : SDL_APP_FAILURE; }

  void App::set_surface_size(int width, int height) {
    surface_width_ = width;
    surface_height_ = height;
  }

  SDL_AppResult App::take_exit_request() {
    const SDL_AppResult rc = pending_exit_;
    pending_exit_ = SDL_APP_CONTINUE;
    return rc;
  }

  // ---------------------------------------------------------------------------
  // SDL_AppInit
  // ---------------------------------------------------------------------------
  SDL_AppResult host_init(App& app, int argc, char** argv) {
    // App first: on_init runs before any SDL/GPU object exists so it can still
    // adjust settings (render mode, title, size), parse argv and attach the
    // GUI layer. SUCCESS exits 0 without entering the loop; FAILURE exits 1
    // (SDL still calls SDL_AppQuit afterwards).
    if (app.callbacks != nullptr) {
      const SDL_AppResult rc = app.callbacks->on_init(app, argc, argv);
      if (rc != SDL_APP_CONTINUE) return rc;
    }

    if (app.settings.render_mode == AppSettings::RenderMode::Windowed) {
      if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
      }

      // Content-scale the configured size (family-A apps, chess main).
      const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
      const int w = static_cast<int>(static_cast<float>(app.settings.width) * scale);
      const int h = static_cast<int>(static_cast<float>(app.settings.height) * scale);

      const SDL_WindowFlags flags = (app.settings.resizable ? SDL_WINDOW_RESIZABLE : 0) |
                                    (app.settings.high_pixel_density ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0);
      app.window = SDL_CreateWindow(app.settings.title, w, h, flags);
      if (app.window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
      }

      int pw = 0, ph = 0;
      SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
      app.set_surface_size(pw > 0 ? pw : w, ph > 0 ? ph : h);

      ContextDesc desc;
      desc.power_preference = app.settings.power_preference;
      desc.backend_type = WGPUBackendType_Undefined;
      desc.want_surface = true;
      desc.window = app.window;
      if (!app.gpu.init(desc)) {
        SDL_Log("WebGPU context init failed");
        return SDL_APP_FAILURE;
      }
    } else {
      // Headless (both modes): no video subsystem, no window. HeadlessNull's
      // offscreen device lands with the headless-mode work; until then both
      // headless modes run the pure-logic loop with no GPU objects at all.
      if (!SDL_Init(0)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
      }
      app.set_surface_size(app.settings.width, app.settings.height);
    }

    if (app.gui() != nullptr && !app.gui()->init(app)) {
      SDL_Log("GUI layer init failed");
      return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
  }

  // ---------------------------------------------------------------------------
  // SDL_AppEvent
  // ---------------------------------------------------------------------------
  namespace {

    // Field translation mirrors apps/rmluidemo's InputState feed.
    void feed_input(App& app, const SDL_Event& e) {
      switch (e.type) {
        case SDL_EVENT_KEY_DOWN: app.input.on_key(e.key.key, true); break;
        case SDL_EVENT_KEY_UP: app.input.on_key(e.key.key, false); break;
        case SDL_EVENT_MOUSE_MOTION: app.input.on_mouse_move(e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: app.input.on_mouse_button(e.button.button, true); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: app.input.on_mouse_button(e.button.button, false); break;
        case SDL_EVENT_MOUSE_WHEEL: app.input.on_wheel(e.wheel.y); break;
        default: break;
      }
    }

    // Surface RECONFIGURE-ONLY resize handling (canonical family-A behavior;
    // no GUI device-object invalidate dance). Returns after handling.
    void handle_window_resize(App& app, const SDL_Event& e) {
      if (e.type != SDL_EVENT_WINDOW_RESIZED && e.type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) return;
      if (app.window == nullptr || e.window.windowID != SDL_GetWindowID(app.window)) return;

      int w = 0, h = 0;
      SDL_GetWindowSizeInPixels(app.window, &w, &h);
      if (w <= 0 || h <= 0) return;
      if (w == app.width() && h == app.height()) return;  // dedupe RESIZED + PIXEL_SIZE_CHANGED pairs

      app.gpu.configure_surface(w, h);
      app.set_surface_size(w, h);
      if (app.gui() != nullptr) app.gui()->on_surface_resized();
    }

  }  // namespace

  SDL_AppResult host_event(App& app, const SDL_Event& event) {
    if (app.gui() != nullptr) app.gui()->process_event(event);
    feed_input(app, event);
    handle_window_resize(app, event);

    if (app.callbacks != nullptr) {
      const SDL_AppResult rc = app.callbacks->on_event(app, event);
      if (rc != SDL_APP_CONTINUE) return rc;
    }

    if (event.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && app.window != nullptr &&
        event.window.windowID == SDL_GetWindowID(app.window))
      return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
  }

  // ---------------------------------------------------------------------------
  // SDL_AppIterate
  // ---------------------------------------------------------------------------
  namespace {

    // Measure the real frame delta; clamp at 100 ms to avoid huge stall spikes
    // (apps/hideandseeksquared measure_delta_seconds).
    float frame_delta_seconds() {
      static Uint64 last = SDL_GetPerformanceCounter();
      const Uint64 now = SDL_GetPerformanceCounter();
      float dt = static_cast<float>(static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency()));
      last = now;
      if (dt > 0.1f) dt = 0.1f;
      return dt;
    }

    bool surface_status_fatal(WGPUSurfaceGetCurrentTextureStatus status) { return status == WGPUSurfaceGetCurrentTextureStatus_Error; }

    SDL_AppResult run_frame(App& app, float dt) {
      if (app.callbacks != nullptr) {
        const SDL_AppResult rc = app.callbacks->on_iterate(app, dt);
        if (rc != SDL_APP_CONTINUE) return rc;
      }
      const SDL_AppResult exit_rc = app.take_exit_request();
      if (exit_rc != SDL_APP_CONTINUE) return exit_rc;

      // GPU frame only when a device AND a window surface exist (Windowed).
      // HeadlessNone runs the pure logic loop: no GPU objects at all.
      if (app.device() == nullptr || app.window == nullptr) return SDL_APP_CONTINUE;

      WGPUSurfaceTexture st = app.gpu.acquire();
      if (surface_status_fatal(st.status)) {
        SDL_Log("Unrecoverable surface texture status=%d", static_cast<int>(st.status));
        return SDL_APP_FAILURE;
      }
      if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal || st.texture == nullptr) {
        // Suboptimal/outdated/lost: drop this frame's texture and reconfigure.
        if (st.texture != nullptr) wgpuTextureRelease(st.texture);
        app.gpu.configure_surface(app.width(), app.height());
        return SDL_APP_CONTINUE;
      }

      WGPUTextureViewDescriptor view_desc = {};
      view_desc.format = app.gpu.surface_format();
      view_desc.dimension = WGPUTextureViewDimension_2D;
      view_desc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
      view_desc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
      view_desc.aspect = WGPUTextureAspect_All;
      WGPUTextureView texture_view = wgpuTextureCreateView(st.texture, &view_desc);

      const float (&c)[4] = app.settings.clear_color;
      WGPURenderPassColorAttachment color_att = {};
      color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
      color_att.loadOp = WGPULoadOp_Clear;
      color_att.storeOp = WGPUStoreOp_Store;
      color_att.clearValue = {c[0] * c[3], c[1] * c[3], c[2] * c[3], c[3]};  // premultiplied (chess)
      color_att.view = texture_view;

      WGPURenderPassDescriptor rp_desc = {};
      rp_desc.colorAttachmentCount = 1;
      rp_desc.colorAttachments = &color_att;
      rp_desc.depthStencilAttachment = nullptr;

      WGPUCommandEncoderDescriptor enc_desc = {};
      WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app.device(), &enc_desc);
      WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);

      // GUI new_frame BEFORE on_draw so app GUI code in on_draw emits into an
      // open GUI frame; gui->render(pass) below submits it (chess order).
      if (app.gui() != nullptr) app.gui()->new_frame();
      if (app.callbacks != nullptr) app.callbacks->on_draw(app, pass);
      if (app.gui() != nullptr) app.gui()->render(pass);

      wgpuRenderPassEncoderEnd(pass);

      WGPUCommandBufferDescriptor cmd_desc = {};
      WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
      wgpuQueueSubmit(app.gpu.queue(), 1, &cmd);

      wgpuTextureViewRelease(texture_view);
      wgpuTextureRelease(st.texture);
      wgpuCommandBufferRelease(cmd);
      wgpuRenderPassEncoderRelease(pass);
      wgpuCommandEncoderRelease(encoder);

      app.gpu.present();
      app.gpu.tick();
      return SDL_APP_CONTINUE;
    }

  }  // namespace

  SDL_AppResult host_iterate(App& app) {
    const SDL_AppResult rc = run_frame(app, frame_delta_seconds());
    // This frame's events were delivered (SDL_AppEvent) and consumed above;
    // clear per-frame input edges now so the next event batch starts fresh —
    // InputState's contract is "clear before feeding", and feeding happens
    // between iterates in the callback model.
    app.input.begin_frame();
    return rc;
  }

  // ---------------------------------------------------------------------------
  // SDL_AppQuit — idempotent; safe on web where (behavior change) it now runs
  // after the emscripten main loop with a possibly lost surface. Runs on every
  // exit path, including init failures and SUCCESS-straight-from-on_init, so
  // every stage must tolerate never-initialized state.
  // ---------------------------------------------------------------------------
  void host_quit(App& app) {
    if (app.callbacks != nullptr) app.callbacks->on_shutdown(app);
    if (app.gui() != nullptr) app.gui()->shutdown();
    app.sched.shutdown();
    app.gpu.shutdown();  // null-checks internally; tolerates a lost surface
    if (app.window != nullptr) {
      SDL_DestroyWindow(app.window);
      app.window = nullptr;
    }
    SDL_Quit();
  }

}  // namespace app
