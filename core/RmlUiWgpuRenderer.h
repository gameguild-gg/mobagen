#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#include <webgpu/webgpu.h>
#include <cstdint>
#include <unordered_map>

// WebGPU render backend for RmlUi.
//
// Implements Rml::RenderInterface using the Dawn / emdawnwebgpu WebGPU API.
// Basic implementation — supports geometry rendering, textures (via SDL3_image),
// and scissor testing. Advanced features (clip masks, layers, filters, shaders)
// are not yet implemented.
//
// Usage (per frame):
//   1. renderer.PrepareFrame(viewportW, viewportH);   // BEFORE pass
//   2. ... begin render pass ...
//   3. renderer.BeginRenderPass(pass);                // with active pass
//   4. context->Render();                             // calls RenderGeometry
//   5. renderer.EndRenderPass();

class RmlUiWgpuRenderer : public Rml::RenderInterface {
public:
  RmlUiWgpuRenderer(WGPUDevice device, WGPUQueue queue,
                    WGPUTextureFormat surfaceFormat);
  ~RmlUiWgpuRenderer() override;

  // Call BEFORE wgpuCommandEncoderBeginRenderPass each frame.
  // Writes the projection uniform for the current viewport.
  void PrepareFrame(int viewportWidth, int viewportHeight);

  // Call AFTER wgpuCommandEncoderBeginRenderPass, before context->Render().
  void BeginRenderPass(WGPURenderPassEncoder pass);
  void EndRenderPass();

  // --- Rml::RenderInterface (required) ---
  Rml::CompiledGeometryHandle CompileGeometry(
      Rml::Span<const Rml::Vertex> vertices,
      Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

  Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions,
                                 const Rml::String& source) override;
  Rml::TextureHandle GenerateTexture(
      Rml::Span<const Rml::byte> source,
      Rml::Vector2i sourceDimensions) override;
  void ReleaseTexture(Rml::TextureHandle textureHandle) override;

  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;

  // --- Rml::RenderInterface (optional, not implemented) ---
  // Clip masks, layers, filters, shaders, transforms — not yet implemented.

private:
  // ---- WebGPU objects owned by the renderer ----
  WGPUDevice  mDevice  = nullptr;
  WGPUQueue   mQueue   = nullptr;
  WGPUTextureFormat mSurfaceFormat = WGPUTextureFormat_Undefined;

  WGPUShaderModule  mShaderModule  = nullptr;
  WGPUBindGroupLayout mBindGroupLayout0 = nullptr; // per-draw uniforms
  WGPUBindGroupLayout mBindGroupLayout1 = nullptr; // texture + sampler
  WGPUPipelineLayout mPipelineLayout = nullptr;
  WGPURenderPipeline mPipeline       = nullptr;
  WGPUSampler       mSampler        = nullptr;
  WGPUTexture       mWhiteTexture   = nullptr;
  WGPUTextureView   mWhiteTextureView = nullptr;
  WGPUBindGroup     mWhiteBindGroup = nullptr;

  // ---- Per-draw uniform buffer ----
  // Pre-allocated buffer with kMaxDrawsPerFrame slots (64 bytes each).
  // Each slot holds a mat4x4f (projection + translation baked).
  // Used with dynamic uniform buffer offsets.
  WGPUBuffer      mPerDrawUniform = nullptr;
  WGPUBindGroup   mPerDrawBindGroup = nullptr;

  // ---- Current render pass state ----
  WGPURenderPassEncoder mCurrentPass = nullptr;
  int  mViewportWidth  = 0;
  int  mViewportHeight = 0;
  int  mDrawCount      = 0;  // number of draw calls this frame
  bool mWarnedThisFrame = false;  // set when we drop a draw past kMaxDrawsPerFrame
  bool mScissorEnabled = false;
  int  mScissorX = 0, mScissorY = 0, mScissorW = 0, mScissorH = 0;

  // ---- Handle storage ----
  struct GeometryData {
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer indexBuffer  = nullptr;
    int        numIndices   = 0;
    size_t     indexBufferSize = 0;
    size_t     vertexBufferSize = 0;
  };
  struct TextureData {
    WGPUTexture     texture = nullptr;
    WGPUTextureView view    = nullptr;
    WGPUBindGroup   bindGroup = nullptr;
    int width  = 0;
    int height = 0;
  };

  std::unordered_map<Rml::CompiledGeometryHandle, GeometryData> mGeometries;
  std::unordered_map<Rml::TextureHandle, TextureData> mTextures;
  Rml::CompiledGeometryHandle mNextGeomHandle = 1;
  Rml::TextureHandle          mNextTexHandle  = 1;

  // ---- Init helpers ----
  void createShader();
  void createBindGroups();
  void createPipeline();
  void createWhiteTexture();
  void createPerDrawUniforms();
};
