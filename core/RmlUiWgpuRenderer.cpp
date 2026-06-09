#include "RmlUiWgpuRenderer.h"

#include <RmlUi/Core/Vertex.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <webgpu/webgpu.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// WGSL shader for RmlUi geometry rendering.
// Uses a single uniform for projection+translation, written per-frame.
static const char kWgslShader[] = R"(
struct PerDraw {
  projAndTrans : mat4x4f,
}

@group(0) @binding(0) var<uniform> u : PerDraw;

struct VSInput {
  @location(0) position : vec2f,
  @location(1) color    : vec4f,
  @location(2) texcoord : vec2f,
}

struct VSOutput {
  @builtin(position) position : vec4f,
  @location(0)      color    : vec4f,
  @location(1)      texcoord : vec2f,
}

@vertex
fn vs_main(in : VSInput) -> VSOutput {
  var out : VSOutput;
  out.position = u.projAndTrans * vec4f(in.position, 0.0, 1.0);
  out.color    = in.color;
  out.texcoord = in.texcoord;
  return out;
}

@group(1) @binding(0) var tex : texture_2d<f32>;
@group(1) @binding(1) var smp : sampler;

@fragment
fn fs_main(in : VSOutput) -> @location(0) vec4f {
  // RmlUi vertex colors are premultiplied. The font texture is also RGBA8
  // with premultiplied alpha. We use only the texture's alpha channel as
  // the glyph mask, and modulate the text color by it.
  let sampled = textureSample(tex, smp, in.texcoord);
  return vec4f(in.color.rgb * sampled.a, sampled.a);
}
)";

// Number of geometry draws we can handle in a single frame.
// Each draw needs its own uniform slot. WebGPU requires dynamic uniform
// buffer offsets to be aligned to 256 bytes, so each slot is 256 bytes
// (only first 64 bytes used for the mat4x4f).
static constexpr int kMaxDrawsPerFrame = 256;
static constexpr uint32_t kUniformSlotSize = 256; // WebGPU min alignment

// ---- Helpers ----

static void wgpuBufferDestroyAndRelease(WGPUBuffer buf) {
  if (!buf) return;
  wgpuBufferDestroy(buf);
  wgpuBufferRelease(buf);
}

// Build an orthographic projection matrix that maps RmlUi pixel coords
// to NDC, with optional translation baked in.
// Y is flipped because RmlUi has Y-down, WebGPU has Y-up.
// Memory layout is column-major (what WGSL expects).
static void buildProjTrans(float outMatColMajor[16], int width, int height,
                           float tx, float ty) {
  memset(outMatColMajor, 0, 16 * sizeof(float));
  // Column 0 (scale X)
  outMatColMajor[0] = 2.0f / (float)width;
  // Column 1 (scale Y, flipped)
  outMatColMajor[5] = -2.0f / (float)height;
  // Column 2 (depth range)
  outMatColMajor[10] = 1.0f;
  // Column 3 (translation: maps 0→-1 and 0→1 respectively, plus user tx/ty)
  outMatColMajor[12] = -1.0f + (2.0f * tx / (float)width);
  outMatColMajor[13] = 1.0f + (-2.0f * ty / (float)height);
  outMatColMajor[15] = 1.0f;
}

// ---- Constructor / Destructor ----

RmlUiWgpuRenderer::RmlUiWgpuRenderer(WGPUDevice device, WGPUQueue queue,
                                     WGPUTextureFormat surfaceFormat)
    : mDevice(device), mQueue(queue), mSurfaceFormat(surfaceFormat) {
  createShader();
  createBindGroups();
  createPipeline();
  createWhiteTexture();
  createPerDrawUniforms();
}

RmlUiWgpuRenderer::~RmlUiWgpuRenderer() {
  for (auto& [_, g] : mGeometries) {
    wgpuBufferDestroyAndRelease(g.vertexBuffer);
    wgpuBufferDestroyAndRelease(g.indexBuffer);
  }
  mGeometries.clear();

  for (auto& [_, t] : mTextures) {
    if (t.bindGroup) wgpuBindGroupRelease(t.bindGroup);
    if (t.view)      wgpuTextureViewRelease(t.view);
    if (t.texture)   wgpuTextureRelease(t.texture);
  }
  mTextures.clear();

  if (mPerDrawBindGroup) wgpuBindGroupRelease(mPerDrawBindGroup);
  if (mPerDrawUniform)   wgpuBufferDestroyAndRelease(mPerDrawUniform);
  if (mWhiteBindGroup)   wgpuBindGroupRelease(mWhiteBindGroup);
  if (mWhiteTextureView) wgpuTextureViewRelease(mWhiteTextureView);
  if (mWhiteTexture)     wgpuTextureRelease(mWhiteTexture);
  if (mSampler)          wgpuSamplerRelease(mSampler);
  if (mPipeline)         wgpuRenderPipelineRelease(mPipeline);
  if (mPipelineLayout)   wgpuPipelineLayoutRelease(mPipelineLayout);
  if (mBindGroupLayout1) wgpuBindGroupLayoutRelease(mBindGroupLayout1);
  if (mBindGroupLayout0) wgpuBindGroupLayoutRelease(mBindGroupLayout0);
  if (mShaderModule)     wgpuShaderModuleRelease(mShaderModule);
}

// ---- Shader ----

void RmlUiWgpuRenderer::createShader() {
  WGPUShaderSourceWGSL wgslDesc = {};
  wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
  wgslDesc.code = {kWgslShader, WGPU_STRLEN};

  WGPUShaderModuleDescriptor smDesc = {};
  smDesc.nextInChain = &wgslDesc.chain;
  mShaderModule = wgpuDeviceCreateShaderModule(mDevice, &smDesc);
  SDL_Log("RmlUiWgpuRenderer: shader module created");
}

// ---- Bind group layouts ----

void RmlUiWgpuRenderer::createBindGroups() {
  // Group 0: projection + translation uniform (mat4x4f, per-draw with dynamic offset)
  {
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = true;
    entry.buffer.minBindingSize = 64; // mat4x4f = 64 bytes

    WGPUBindGroupLayoutDescriptor desc = {};
    desc.entryCount = 1;
    desc.entries = &entry;
    mBindGroupLayout0 = wgpuDeviceCreateBindGroupLayout(mDevice, &desc);
  }

  // Group 1: texture + sampler
  {
    WGPUBindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[0].texture.multisampled = false;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor desc = {};
    desc.entryCount = 2;
    desc.entries = entries;
    mBindGroupLayout1 = wgpuDeviceCreateBindGroupLayout(mDevice, &desc);
  }
}

// ---- Pipeline ----

void RmlUiWgpuRenderer::createPipeline() {
  WGPUBindGroupLayout layouts[] = {mBindGroupLayout0, mBindGroupLayout1};
  WGPUPipelineLayoutDescriptor plDesc = {};
  plDesc.bindGroupLayoutCount = 2;
  plDesc.bindGroupLayouts = layouts;
  mPipelineLayout = wgpuDeviceCreatePipelineLayout(mDevice, &plDesc);

  // Vertex attributes matching Rml::Vertex layout (20 bytes)
  WGPUVertexAttribute attrs[3] = {};
  attrs[0].format = WGPUVertexFormat_Float32x2;
  attrs[0].offset = 0;
  attrs[0].shaderLocation = 0;
  attrs[1].format = WGPUVertexFormat_Unorm8x4;
  attrs[1].offset = 8;
  attrs[1].shaderLocation = 1;
  attrs[2].format = WGPUVertexFormat_Float32x2;
  attrs[2].offset = 12;
  attrs[2].shaderLocation = 2;

  WGPUVertexBufferLayout vbLayout = {};
  vbLayout.arrayStride = 20;
  vbLayout.stepMode = WGPUVertexStepMode_Vertex;
  vbLayout.attributeCount = 3;
  vbLayout.attributes = attrs;

  // Premultiplied alpha blending
  WGPUBlendState blend = {};
  blend.color.operation = WGPUBlendOperation_Add;
  blend.color.srcFactor = WGPUBlendFactor_One;
  blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
  blend.alpha.operation = WGPUBlendOperation_Add;
  blend.alpha.srcFactor = WGPUBlendFactor_One;
  blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

  WGPUColorTargetState colorTarget = {};
  colorTarget.format = mSurfaceFormat;
  colorTarget.blend = &blend;
  colorTarget.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragState = {};
  fragState.module = mShaderModule;
  fragState.entryPoint = {"fs_main", WGPU_STRLEN};
  fragState.targetCount = 1;
  fragState.targets = &colorTarget;

  // Multisample state
  WGPUMultisampleState multisample = {};
  multisample.count = 1;
  multisample.mask = 0xFFFFFFFF;

  WGPURenderPipelineDescriptor pipeDesc = {};
  pipeDesc.layout = mPipelineLayout;
  pipeDesc.vertex.module = mShaderModule;
  pipeDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
  pipeDesc.vertex.bufferCount = 1;
  pipeDesc.vertex.buffers = &vbLayout;
  pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  pipeDesc.multisample = multisample;
  pipeDesc.fragment = &fragState;

  mPipeline = wgpuDeviceCreateRenderPipeline(mDevice, &pipeDesc);
  SDL_Log("RmlUiWgpuRenderer: pipeline created");

  // Sampler
  WGPUSamplerDescriptor sampDesc = {};
  sampDesc.addressModeU  = WGPUAddressMode_ClampToEdge;
  sampDesc.addressModeV  = WGPUAddressMode_ClampToEdge;
  sampDesc.addressModeW  = WGPUAddressMode_ClampToEdge;
  sampDesc.magFilter     = WGPUFilterMode_Linear;
  sampDesc.minFilter     = WGPUFilterMode_Linear;
  sampDesc.maxAnisotropy = 1;
  mSampler = wgpuDeviceCreateSampler(mDevice, &sampDesc);
}

// ---- White texture (used when rendering non-textured geometry) ----

void RmlUiWgpuRenderer::createWhiteTexture() {
  const uint8_t white[4] = {255, 255, 255, 255};

  WGPUTextureDescriptor texDesc = {};
  texDesc.size.width = 1;
  texDesc.size.height = 1;
  texDesc.size.depthOrArrayLayers = 1;
  texDesc.mipLevelCount = 1;
  texDesc.sampleCount = 1;
  texDesc.dimension = WGPUTextureDimension_2D;
  texDesc.format = WGPUTextureFormat_RGBA8Unorm;
  texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  mWhiteTexture = wgpuDeviceCreateTexture(mDevice, &texDesc);

  WGPUTexelCopyTextureInfo dst = {};
  dst.texture = mWhiteTexture;
  dst.mipLevel = 0;
  dst.origin = {0, 0, 0};
  dst.aspect = WGPUTextureAspect_All;

  WGPUTexelCopyBufferLayout dataLayout = {};
  dataLayout.bytesPerRow = 4;
  dataLayout.rowsPerImage = 1;

  WGPUExtent3D writeSize = {1, 1, 1};
  wgpuQueueWriteTexture(mQueue, &dst, white, 4, &dataLayout, &writeSize);

  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.baseMipLevel = 0;
  viewDesc.mipLevelCount = 1;
  viewDesc.baseArrayLayer = 0;
  viewDesc.arrayLayerCount = 1;
  mWhiteTextureView = wgpuTextureCreateView(mWhiteTexture, &viewDesc);

  WGPUBindGroupEntry entries[2] = {};
  entries[0].binding = 0;
  entries[0].textureView = mWhiteTextureView;
  entries[1].binding = 1;
  entries[1].sampler = mSampler;

  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = mBindGroupLayout1;
  bgDesc.entryCount = 2;
  bgDesc.entries = entries;
  mWhiteBindGroup = wgpuDeviceCreateBindGroup(mDevice, &bgDesc);
}

// ---- Per-draw uniform buffer ----
// Allocates room for kMaxDrawsPerFrame entries (each `kUniformSlotSize` of
// bytes, only the first 64 are used). During rendering, each
// `RenderGeometry` call writes its matrix to the next slot before the draw
// call and binds the corresponding dynamic offset.
//
// Writes are issued via `wgpuQueueWriteBuffer` and are NOT synchronous.
// The reason this works inside an active render pass is that WebGPU
// guarantees that queue operations and render-pass operations on the same
// queue are observed in submission order, so the write is visible to any
// `SetBindGroup` / `DrawIndexed` that follows it in the same pass. If you
// ever move these writes to a different queue or thread, this invariant
// breaks — corruption will be silent.

void RmlUiWgpuRenderer::createPerDrawUniforms() {
  const size_t bufSize = kMaxDrawsPerFrame * kUniformSlotSize;
  WGPUBufferDescriptor bufDesc = {};
  bufDesc.size = bufSize;
  bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  mPerDrawUniform = wgpuDeviceCreateBuffer(mDevice, &bufDesc);

  // Pre-fill with identity matrices
  float identity[16] = {};
  identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
  uint8_t* zeros = new uint8_t[bufSize];
  for (int i = 0; i < kMaxDrawsPerFrame; i++) {
    memcpy(zeros + i * kUniformSlotSize, identity, 64);
  }
  wgpuQueueWriteBuffer(mQueue, mPerDrawUniform, 0, zeros, bufSize);
  delete[] zeros;

  // Bind group covering the whole buffer with dynamic offset
  WGPUBindGroupEntry entry = {};
  entry.binding = 0;
  entry.buffer = mPerDrawUniform;
  entry.offset = 0;
  entry.size = kUniformSlotSize;

  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = mBindGroupLayout0;
  bgDesc.entryCount = 1;
  bgDesc.entries = &entry;
  mPerDrawBindGroup = wgpuDeviceCreateBindGroup(mDevice, &bgDesc);
}

// ---- PrepareFrame / BeginRenderPass / EndRenderPass ----

void RmlUiWgpuRenderer::PrepareFrame(int viewportWidth, int viewportHeight,
                                     int physicalWidth, int physicalHeight) {
  mViewportWidth  = viewportWidth;
  mViewportHeight = viewportHeight;
  // Fall back to logical size when physical is not provided (non-HiDPI).
  mPhysicalWidth  = physicalWidth  > 0 ? physicalWidth  : viewportWidth;
  mPhysicalHeight = physicalHeight > 0 ? physicalHeight : viewportHeight;
  mDrawCount = 0;
  mWarnedThisFrame = false;
  mScissorEnabled = false;
  mScissorX = mScissorY = 0;
  mScissorW = mScissorH = 0;
}

void RmlUiWgpuRenderer::BeginRenderPass(WGPURenderPassEncoder pass) {
  mCurrentPass = pass;
  // Scissor rect must be in physical (device) pixels.
  wgpuRenderPassEncoderSetScissorRect(pass, 0, 0,
                                      (uint32_t)mPhysicalWidth,
                                      (uint32_t)mPhysicalHeight);
}

void RmlUiWgpuRenderer::EndRenderPass() {
  mCurrentPass = nullptr;
}

// ---- Geometry compilation ----

Rml::CompiledGeometryHandle RmlUiWgpuRenderer::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices) {

  GeometryData geom;
  geom.numIndices = (int)indices.size();

  {
    WGPUBufferDescriptor desc = {};
    desc.size = vertices.size() * sizeof(Rml::Vertex);
    if (desc.size == 0) desc.size = sizeof(Rml::Vertex);
    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    geom.vertexBuffer = wgpuDeviceCreateBuffer(mDevice, &desc);
    geom.vertexBufferSize = desc.size;
    if (geom.vertexBuffer && vertices.size() > 0) {
      wgpuQueueWriteBuffer(mQueue, geom.vertexBuffer, 0, vertices.data(), desc.size);
    }
  }

  {
    WGPUBufferDescriptor desc = {};
    desc.size = indices.size() * sizeof(uint32_t);
    if (desc.size == 0) desc.size = sizeof(uint32_t);
    desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    geom.indexBuffer = wgpuDeviceCreateBuffer(mDevice, &desc);
    geom.indexBufferSize = desc.size;
    if (geom.indexBuffer && indices.size() > 0) {
      wgpuQueueWriteBuffer(mQueue, geom.indexBuffer, 0, indices.data(), desc.size);
    }
  }

  Rml::CompiledGeometryHandle handle = mNextGeomHandle++;
  mGeometries[handle] = geom;
  return handle;
}

void RmlUiWgpuRenderer::RenderGeometry(
    Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation,
    Rml::TextureHandle texture) {

  if (!mCurrentPass) return;
  if (mDrawCount >= kMaxDrawsPerFrame) {
    // Warn once per frame so a complex document with too many draw calls
    // surfaces as a diagnostic rather than as silently missing geometry.
    if (!mWarnedThisFrame) {
      mWarnedThisFrame = true;
      SDL_Log("RmlUiWgpuRenderer: kMaxDrawsPerFrame (%d) exceeded, "
              "further draws dropped this frame. Raise the limit in "
              "RmlUiWgpuRenderer.h if this happens often.",
              kMaxDrawsPerFrame);
    }
    return;
  }

  auto it = mGeometries.find(geometry);
  if (it == mGeometries.end()) return;

  const auto& geom = it->second;

  // Build projection with translation baked in
  float projTrans[16];
  buildProjTrans(projTrans, mViewportWidth, mViewportHeight,
                 translation.x, translation.y);

  // Write to the per-draw uniform slot
  uint32_t slotOffset = (uint32_t)(mDrawCount * kUniformSlotSize);
  wgpuQueueWriteBuffer(mQueue, mPerDrawUniform, slotOffset,
                       projTrans, 64);

  // Bind pipeline and uniform
  wgpuRenderPassEncoderSetPipeline(mCurrentPass, mPipeline);
  wgpuRenderPassEncoderSetBindGroup(mCurrentPass, 0,
                                    mPerDrawBindGroup, 1,
                                    &slotOffset);

  // Texture bind group
  if (texture != 0) {
    auto texIt = mTextures.find(texture);
    if (texIt != mTextures.end()) {
      wgpuRenderPassEncoderSetBindGroup(mCurrentPass, 1,
                                        texIt->second.bindGroup, 0, nullptr);
    } else {
      wgpuRenderPassEncoderSetBindGroup(mCurrentPass, 1,
                                        mWhiteBindGroup, 0, nullptr);
    }
  } else {
    wgpuRenderPassEncoderSetBindGroup(mCurrentPass, 1,
                                      mWhiteBindGroup, 0, nullptr);
  }

  wgpuRenderPassEncoderSetVertexBuffer(mCurrentPass, 0, geom.vertexBuffer, 0,
                                       geom.vertexBufferSize);
  wgpuRenderPassEncoderSetIndexBuffer(mCurrentPass, geom.indexBuffer,
                                      WGPUIndexFormat_Uint32, 0,
                                      geom.indexBufferSize);

  if (mScissorEnabled) {
    wgpuRenderPassEncoderSetScissorRect(mCurrentPass,
                                        (uint32_t)mScissorX,
                                        (uint32_t)mScissorY,
                                        (uint32_t)mScissorW,
                                        (uint32_t)mScissorH);
  }

  wgpuRenderPassEncoderDrawIndexed(mCurrentPass,
                                   (uint32_t)geom.numIndices, 1, 0, 0, 0);
  mDrawCount++;
}

void RmlUiWgpuRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  auto it = mGeometries.find(geometry);
  if (it == mGeometries.end()) return;
  wgpuBufferDestroyAndRelease(it->second.vertexBuffer);
  wgpuBufferDestroyAndRelease(it->second.indexBuffer);
  mGeometries.erase(it);
}

// ---- Textures ----

Rml::TextureHandle RmlUiWgpuRenderer::LoadTexture(
    Rml::Vector2i& textureDimensions,
    const Rml::String& source) {

  SDL_IOStream* io = SDL_IOFromFile(source.c_str(), "rb");
  if (!io) return 0;

  SDL_Surface* surface = IMG_Load_IO(io, true);
  if (!surface) return 0;

  SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA8888);
  if (!rgba) {
    SDL_DestroySurface(surface);
    return 0;
  }

  textureDimensions.x = rgba->w;
  textureDimensions.y = rgba->h;

  TextureData texData;
  texData.width = rgba->w;
  texData.height = rgba->h;

  WGPUTextureDescriptor texDesc = {};
  texDesc.size.width = (uint32_t)rgba->w;
  texDesc.size.height = (uint32_t)rgba->h;
  texDesc.size.depthOrArrayLayers = 1;
  texDesc.mipLevelCount = 1;
  texDesc.sampleCount = 1;
  texDesc.dimension = WGPUTextureDimension_2D;
  texDesc.format = WGPUTextureFormat_RGBA8Unorm;
  texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  texData.texture = wgpuDeviceCreateTexture(mDevice, &texDesc);

  WGPUTexelCopyTextureInfo dst = {};
  dst.texture = texData.texture;
  dst.mipLevel = 0;
  dst.origin = {0, 0, 0};
  dst.aspect = WGPUTextureAspect_All;

  WGPUTexelCopyBufferLayout dataLayout = {};
  dataLayout.bytesPerRow = (uint32_t)(rgba->pitch);
  dataLayout.rowsPerImage = (uint32_t)rgba->h;

  WGPUExtent3D writeSize = {(uint32_t)rgba->w, (uint32_t)rgba->h, 1};
  wgpuQueueWriteTexture(mQueue, &dst, rgba->pixels,
                        (size_t)(rgba->pitch * rgba->h),
                        &dataLayout, &writeSize);

  SDL_DestroySurface(rgba);
  SDL_DestroySurface(surface);

  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.baseMipLevel = 0;
  viewDesc.mipLevelCount = 1;
  viewDesc.baseArrayLayer = 0;
  viewDesc.arrayLayerCount = 1;
  texData.view = wgpuTextureCreateView(texData.texture, &viewDesc);

  WGPUBindGroupEntry entries[2] = {};
  entries[0].binding = 0;
  entries[0].textureView = texData.view;
  entries[1].binding = 1;
  entries[1].sampler = mSampler;

  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = mBindGroupLayout1;
  bgDesc.entryCount = 2;
  bgDesc.entries = entries;
  texData.bindGroup = wgpuDeviceCreateBindGroup(mDevice, &bgDesc);

  Rml::TextureHandle handle = mNextTexHandle++;
  mTextures[handle] = texData;
  return handle;
}

Rml::TextureHandle RmlUiWgpuRenderer::GenerateTexture(
    Rml::Span<const Rml::byte> source,
    Rml::Vector2i sourceDimensions) {

  if (source.size() == 0 || sourceDimensions.x <= 0 ||
      sourceDimensions.y <= 0)
    return 0;

  const int w = sourceDimensions.x;
  const int h = sourceDimensions.y;
  const size_t numBytes = source.size();

  // RmlUi provides RGBA in memory order. Our texture format is RGBA8Unorm
  // so we can copy the bytes directly (no swizzle needed).
  const uint8_t* rgbaData = source.data();

  TextureData texData;
  texData.width = w;
  texData.height = h;

  WGPUTextureDescriptor texDesc = {};
  texDesc.size.width = (uint32_t)w;
  texDesc.size.height = (uint32_t)h;
  texDesc.size.depthOrArrayLayers = 1;
  texDesc.mipLevelCount = 1;
  texDesc.sampleCount = 1;
  texDesc.dimension = WGPUTextureDimension_2D;
  texDesc.format = WGPUTextureFormat_RGBA8Unorm;
  texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  texData.texture = wgpuDeviceCreateTexture(mDevice, &texDesc);

  WGPUTexelCopyTextureInfo dst = {};
  dst.texture = texData.texture;
  dst.mipLevel = 0;
  dst.origin = {0, 0, 0};
  dst.aspect = WGPUTextureAspect_All;

  WGPUTexelCopyBufferLayout dataLayout = {};
  dataLayout.bytesPerRow = (uint32_t)(w * 4);
  dataLayout.rowsPerImage = (uint32_t)h;

  WGPUExtent3D writeSize = {(uint32_t)w, (uint32_t)h, 1};
  wgpuQueueWriteTexture(mQueue, &dst, rgbaData, numBytes, &dataLayout, &writeSize);

  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.baseMipLevel = 0;
  viewDesc.mipLevelCount = 1;
  viewDesc.baseArrayLayer = 0;
  viewDesc.arrayLayerCount = 1;
  texData.view = wgpuTextureCreateView(texData.texture, &viewDesc);

  WGPUBindGroupEntry entries[2] = {};
  entries[0].binding = 0;
  entries[0].textureView = texData.view;
  entries[1].binding = 1;
  entries[1].sampler = mSampler;

  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = mBindGroupLayout1;
  bgDesc.entryCount = 2;
  bgDesc.entries = entries;
  texData.bindGroup = wgpuDeviceCreateBindGroup(mDevice, &bgDesc);

  Rml::TextureHandle handle = mNextTexHandle++;
  mTextures[handle] = texData;
  return handle;
}

void RmlUiWgpuRenderer::ReleaseTexture(Rml::TextureHandle textureHandle) {
  auto it = mTextures.find(textureHandle);
  if (it == mTextures.end()) return;
  if (it->second.bindGroup) wgpuBindGroupRelease(it->second.bindGroup);
  if (it->second.view)      wgpuTextureViewRelease(it->second.view);
  if (it->second.texture)   wgpuTextureRelease(it->second.texture);
  mTextures.erase(it);
}

// ---- Scissor ----

void RmlUiWgpuRenderer::EnableScissorRegion(bool enable) {
  mScissorEnabled = enable;
  if (!enable && mCurrentPass) {
    // Reset to full physical viewport when disabling per-element scissor.
    wgpuRenderPassEncoderSetScissorRect(mCurrentPass, 0, 0,
                                        (uint32_t)mPhysicalWidth,
                                        (uint32_t)mPhysicalHeight);
  }
}

void RmlUiWgpuRenderer::SetScissorRegion(Rml::Rectanglei region) {
  // RmlUi provides the scissor rect in logical (context) coordinates.
  // WebGPU requires device (physical) pixel coordinates, so scale by the
  // DPI factor (physical / logical).
  const float scaleX = (mViewportWidth  > 0) ? (float)mPhysicalWidth  / (float)mViewportWidth  : 1.f;
  const float scaleY = (mViewportHeight > 0) ? (float)mPhysicalHeight / (float)mViewportHeight : 1.f;

  mScissorX = (int)(region.Left()   * scaleX);
  mScissorY = (int)(region.Top()    * scaleY);
  mScissorW = (int)(region.Width()  * scaleX);
  mScissorH = (int)(region.Height() * scaleY);

  // Clamp to physical viewport bounds to satisfy WebGPU validation.
  if (mScissorX < 0) mScissorX = 0;
  if (mScissorY < 0) mScissorY = 0;
  if (mScissorX + mScissorW > mPhysicalWidth)  mScissorW = mPhysicalWidth  - mScissorX;
  if (mScissorY + mScissorH > mPhysicalHeight) mScissorH = mPhysicalHeight - mScissorY;
  if (mScissorW < 0) mScissorW = 0;
  if (mScissorH < 0) mScissorH = 0;
}
