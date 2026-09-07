// ============================================================================
// RmlUiLayer — RmlUi (HTML/CSS UI) on the core app host, rendered through
// ImGui's WebGPU pipeline.
// ============================================================================
// RmlImGuiRenderer and the boot/font/frame/teardown sequence moved verbatim
// from the proven apps/rmluidemo/main.cpp (RmlImGuiRenderer 267-434, boot
// 543-587, teardown 706-712): RmlUi layout geometry (CSS backgrounds,
// borders, text glyphs) is routed through ImGui's draw list API so
// imgui_impl_wgpu submits it via the same WebGPU render pass as the rest of
// the frame — ImGui is the GPU host, no separate RmlUi-WebGPU renderer.
// The ImGui side is delegated to a composed ImGuiLayer (core_imgui), which
// owns the SDL3 platform backend + WebGPU renderer backend and their
// headless guards.
#include "rmlui/rmlui_layer.hpp"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include <imgui.h>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Debugger/FontSource.h>  // courier_prime_code[]
#include <RmlUi_Platform_SDL.h>         // SystemInterface_SDL, RmlSDL::InputEventHandler

#include <cstring>
#include <unordered_map>
#include <vector>

namespace app {

  // -------------------------------------------------------------------------
  // RmlImGuiRenderer — Rml::RenderInterface via ImGui background draw list.
  //
  // Untextured geometry (solid CSS colours): uses ImGui's font-atlas white
  // pixel UV so vertex colours pass through unmodified.
  // Textured geometry (font glyphs): WGPUTextureView cast as ImTextureID;
  // imgui_impl_wgpu creates the required bind group automatically.
  // -------------------------------------------------------------------------
  class RmlImGuiRenderer : public Rml::RenderInterface {
  public:
    void set_device(WGPUDevice dev, WGPUQueue q) {
      device_ = dev;
      queue_ = q;
    }

    // ----- geometry --------------------------------------------------------
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
      const auto handle = static_cast<Rml::CompiledGeometryHandle>(++next_id_);
      Geometry& geo = geometries_[handle];
      geo.vertices.assign(vertices.begin(), vertices.end());
      geo.indices.assign(indices.begin(), indices.end());
      return handle;
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { geometries_.erase(handle); }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
      auto it = geometries_.find(handle);
      if (it == geometries_.end()) return;
      const Geometry& geo = it->second;

      ImDrawList* dl = ImGui::GetBackgroundDrawList();

      // Apply per-call clip rect if the scissor is active.
      if (scissor_enabled_) {
        dl->PushClipRect(ImVec2(static_cast<float>(scissor_.Left()), static_cast<float>(scissor_.Top())),
                         ImVec2(static_cast<float>(scissor_.Right()), static_cast<float>(scissor_.Bottom())), true);
      }

      // Untextured (CSS solid colours): keep the draw list's implicit default
      // texture (the ImGui font atlas) and use its white-pixel UV so vertex
      // colour passes through unmodified.  Textured (font glyphs): push the
      // WGPUTextureView stored as TextureHandle; imgui_impl_wgpu creates the
      // required bind group automatically.
      const bool use_white = (texture == 0);
      const ImVec2 white_uv = ImGui::GetIO().Fonts->TexUvWhitePixel;

      if (!use_white) dl->PushTextureID(static_cast<ImTextureID>(texture));

      const int vtx_count = static_cast<int>(geo.vertices.size());
      const int idx_count = static_cast<int>(geo.indices.size());
      const ImDrawIdx vtx_base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
      dl->PrimReserve(idx_count, vtx_count);

      for (const Rml::Vertex& v : geo.vertices) {
        dl->_VtxWritePtr->pos = ImVec2(v.position.x + translation.x, v.position.y + translation.y);
        dl->_VtxWritePtr->uv = use_white ? white_uv : ImVec2(v.tex_coord.x, v.tex_coord.y);
        dl->_VtxWritePtr->col = IM_COL32(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha);
        ++dl->_VtxWritePtr;
      }
      dl->_VtxCurrentIdx += static_cast<unsigned int>(vtx_count);

      for (int idx : geo.indices) {
        *dl->_IdxWritePtr++ = static_cast<ImDrawIdx>(vtx_base + idx);
      }

      if (!use_white) dl->PopTextureID();
      if (scissor_enabled_) dl->PopClipRect();
    }

    // ----- textures --------------------------------------------------------
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override {
      if (!device_ || !queue_) return {};

      const uint32_t w = static_cast<uint32_t>(source_dimensions.x);
      const uint32_t h = static_cast<uint32_t>(source_dimensions.y);
      const uint32_t tight_bpr = w * 4u;
      // WebGPU requires bytesPerRow to be a multiple of 256.
      const uint32_t aligned_bpr = (tight_bpr + 255u) & ~255u;

      std::vector<Rml::byte> padded;
      const Rml::byte* upload_data = source.data();
      if (aligned_bpr != tight_bpr) {
        padded.resize(static_cast<size_t>(aligned_bpr) * h, 0);
        for (uint32_t row = 0; row < h; ++row) {
          std::memcpy(padded.data() + row * aligned_bpr, source.data() + row * tight_bpr, tight_bpr);
        }
        upload_data = padded.data();
      }

      WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
      texDesc.label = {"rmlui_font_tex", WGPU_STRLEN};
      texDesc.dimension = WGPUTextureDimension_2D;
      texDesc.size.width = w;
      texDesc.size.height = h;
      texDesc.size.depthOrArrayLayers = 1;
      texDesc.format = WGPUTextureFormat_RGBA8Unorm;
      texDesc.mipLevelCount = 1;
      texDesc.sampleCount = 1;
      texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
      WGPUTexture tex = wgpuDeviceCreateTexture(device_, &texDesc);
      if (!tex) return {};

      WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
      dst.texture = tex;
      dst.aspect = WGPUTextureAspect_All;

      WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
      layout.bytesPerRow = aligned_bpr;
      layout.rowsPerImage = h;

      WGPUExtent3D extent = WGPU_EXTENT_3D_INIT;
      extent.width = w;
      extent.height = h;
      extent.depthOrArrayLayers = 1;

      const size_t upload_size = static_cast<size_t>(aligned_bpr) * h;
      wgpuQueueWriteTexture(queue_, &dst, upload_data, upload_size, &layout, &extent);

      WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
      viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
      viewDesc.dimension = WGPUTextureViewDimension_2D;
      viewDesc.mipLevelCount = 1;
      viewDesc.arrayLayerCount = 1;
      viewDesc.aspect = WGPUTextureAspect_All;
      WGPUTextureView view = wgpuTextureCreateView(tex, &viewDesc);
      if (!view) {
        wgpuTextureDestroy(tex);
        wgpuTextureRelease(tex);
        return {};
      }

      // TextureHandle stores the WGPUTextureView pointer as a uintptr_t.
      // imgui_impl_wgpu casts ImTextureID back to WGPUTextureView and
      // creates the required bind group automatically.
      const Rml::TextureHandle handle = static_cast<Rml::TextureHandle>(reinterpret_cast<uintptr_t>(view));
      textures_[handle] = tex;
      return handle;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& /*dims*/, const Rml::String& /*src*/) override {
      return {};  // file-based textures not needed for the CSS-only documents
    }

    void ReleaseTexture(Rml::TextureHandle handle) override {
      auto it = textures_.find(handle);
      if (it == textures_.end()) return;
      WGPUTexture tex = it->second;
      auto* view = reinterpret_cast<WGPUTextureView>(static_cast<uintptr_t>(handle));
      if (view) wgpuTextureViewRelease(view);
      if (tex) {
        wgpuTextureDestroy(tex);
        wgpuTextureRelease(tex);
      }
      textures_.erase(it);
    }

    // ----- scissor ---------------------------------------------------------
    void EnableScissorRegion(bool enable) override { scissor_enabled_ = enable; }

    void SetScissorRegion(Rml::Rectanglei region) override { scissor_ = region; }

  private:
    struct Geometry {
      std::vector<Rml::Vertex> vertices;
      std::vector<int> indices;
    };

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    int next_id_ = 0;
    std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometries_;
    std::unordered_map<Rml::TextureHandle, WGPUTexture> textures_;
    bool scissor_enabled_ = false;
    Rml::Rectanglei scissor_{};
  };

  // -------------------------------------------------------------------------
  // RmlUiLayer
  // -------------------------------------------------------------------------
  RmlUiLayer::RmlUiLayer() = default;
  RmlUiLayer::~RmlUiLayer() = default;

  bool RmlUiLayer::init(App& app) {
    app_ = &app;

    // ImGui first: the bridge draws RmlUi geometry into ImGui's background
    // draw list, so the ImGui context + backends must be up. ImGuiLayer
    // handles the headless guards and fails cleanly without a device.
    if (!imgui_layer_.init(app)) return false;

    system_interface_ = std::make_unique<SystemInterface_SDL>();
    // Window is optional: it only drives mouse cursors / text-input IME.
    if (app.window != nullptr) system_interface_->SetWindow(app.window);

    renderer_ = std::make_unique<RmlImGuiRenderer>();
    renderer_->set_device(app.device(), app.gpu.queue());

    Rml::SetSystemInterface(system_interface_.get());
    Rml::SetRenderInterface(renderer_.get());

    if (!Rml::Initialise()) {
      SDL_Log("Rml::Initialise() failed");
      return false;
    }
    rml_initialised_ = true;

    // Load the embedded Courier Prime Code font as "AppFont" (used by the RML).
    Rml::LoadFontFace(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code), sizeof(courier_prime_code)), "AppFont",
                      Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);

    // Load italic variant so <em> elements render correctly.
    Rml::LoadFontFace(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code_italic), sizeof(courier_prime_code_italic)),
                      "AppFont", Rml::Style::FontStyle::Italic, Rml::Style::FontWeight::Normal);

    // Load monospace variant for diagnostic elements.
    Rml::LoadFontFace(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code), sizeof(courier_prime_code)), "monospace",
                      Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);

    context_ = Rml::CreateContext("main", Rml::Vector2i(app.width(), app.height()));
    if (context_ == nullptr) {
      SDL_Log("Rml::CreateContext failed");
      return false;
    }

    Rml::Debugger::Initialise(context_);
    SDL_Log("RmlUiLayer: context 'main' ready (%dx%d)", app.width(), app.height());
    return true;
  }

  void RmlUiLayer::process_event(const SDL_Event& event) {
    imgui_layer_.process_event(event);
    // RmlSDL::InputEventHandler drives SDL cursor/text-input state off the
    // window — headless modes have none, layout does not need the feed.
    if (context_ == nullptr || app_ == nullptr || app_->window == nullptr) return;
    SDL_Event copy = event;  // RmlSDL takes SDL_Event& (non-const)
    RmlSDL::InputEventHandler(context_, app_->window, copy);
  }

  void RmlUiLayer::new_frame() {
    // Open the ImGui frame first — the bridge emits into its background draw
    // list (rmluidemo frame order: Update, NewFrame, ctx->Render, Render).
    imgui_layer_.new_frame();
    if (context_ == nullptr) return;
    context_->Update();    // layout update
    context_->Render();    // geometry -> ImGui background draw list
  }

  void RmlUiLayer::render(WGPURenderPassEncoder pass) { imgui_layer_.render(pass); }

  void RmlUiLayer::on_surface_resized() {
    if (context_ == nullptr || app_ == nullptr) return;
    context_->SetDimensions(Rml::Vector2i(app_->width(), app_->height()));
  }

  void RmlUiLayer::shutdown() {
    // Reverse init order (rmluidemo teardown): Rml::Shutdown destroys the
    // contexts/documents and releases their geometry + textures through the
    // renderer, so it must run while the renderer is still alive.
    if (rml_initialised_) Rml::Shutdown();
    rml_initialised_ = false;
    context_ = nullptr;
    renderer_.reset();
    system_interface_.reset();
    imgui_layer_.shutdown();
  }

}  // namespace app
