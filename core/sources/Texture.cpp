#include "Texture.h"
#include "Renderer2D.h"
#include "engine/Engine.h"
#include "Window.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <webgpu/webgpu.h>

#include <cstring>
#include <stdexcept>

namespace {

// Create a WGPUTexture (RGBA8 UNORM SRGB) sized w*h with CopyDst + TextureBinding usage.
WGPUTexture createRgba8Texture(WGPUDevice device, uint32_t w, uint32_t h, const char* label) {
  WGPUTextureDescriptor desc = {};
  desc.label         = {label, WGPU_STRLEN};
  desc.usage         = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
  desc.dimension     = WGPUTextureDimension_2D;
  desc.size          = {w, h, 1};
  desc.format        = WGPUTextureFormat_RGBA8Unorm;
  desc.mipLevelCount = 1;
  desc.sampleCount   = 1;
  return wgpuDeviceCreateTexture(device, &desc);
}

WGPUTextureView createDefaultView(WGPUTexture tex) {
  WGPUTextureViewDescriptor vd = {};
  vd.format          = WGPUTextureFormat_RGBA8Unorm;
  vd.dimension       = WGPUTextureViewDimension_2D;
  vd.baseMipLevel    = 0;
  vd.mipLevelCount   = 1;
  vd.baseArrayLayer  = 0;
  vd.arrayLayerCount = 1;
  vd.aspect          = WGPUTextureAspect_All;
  return wgpuTextureCreateView(tex, &vd);
}

void uploadRgba8(WGPUQueue queue, WGPUTexture tex, uint32_t w, uint32_t h, const void* pixels) {
  WGPUTexelCopyTextureInfo dst = {};
  dst.texture  = tex;
  dst.mipLevel = 0;
  dst.origin   = {0, 0, 0};
  dst.aspect   = WGPUTextureAspect_All;

  WGPUTexelCopyBufferLayout layout = {};
  layout.offset       = 0;
  layout.bytesPerRow  = 4 * w;
  layout.rowsPerImage = h;

  WGPUExtent3D extent = {w, h, 1};
  wgpuQueueWriteTexture(queue, &dst, pixels, (size_t)4 * w * h, &layout, &extent);
}

} // namespace

Texture* Texture::LoadSVGFromString(const std::string& svgtxt) {
  auto* engine = Engine::GetInstance();
  if (!engine || !engine->window) {
    SDL_Log("Texture::LoadSVGFromString called before Window exists");
    return nullptr;
  }
  WGPUDevice device = engine->window->wgpuDevice;
  WGPUQueue  queue  = engine->window->wgpuQueue;

  SDL_IOStream* io = SDL_IOFromConstMem(svgtxt.data(), svgtxt.size());
  if (!io) {
    SDL_Log("SDL_IOFromConstMem failed: %s", SDL_GetError());
    return nullptr;
  }
  SDL_Surface* loaded = IMG_Load_IO(io, /*closeio=*/true);
  if (!loaded) {
    SDL_Log("IMG_Load_IO failed: %s", SDL_GetError());
    return nullptr;
  }

  // Force RGBA8 layout so we can upload straight to WGPU.
  SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  if (!rgba) {
    SDL_Log("SDL_ConvertSurface failed: %s", SDL_GetError());
    return nullptr;
  }

  auto* tex = new Texture();
  tex->dimensions = {rgba->w, rgba->h};
  tex->texture    = createRgba8Texture(device, rgba->w, rgba->h, "Texture::SVG");
  tex->view       = createDefaultView(tex->texture);
  tex->imTextureID = (ImTextureIDStorage)(uintptr_t)tex->view;

  // SDL_Surface may have row padding (pitch != w*4). Pack into a tight buffer.
  if (rgba->pitch == rgba->w * 4) {
    uploadRgba8(queue, tex->texture, rgba->w, rgba->h, rgba->pixels);
  } else {
    const size_t row = (size_t)rgba->w * 4;
    auto* tight = (uint8_t*)SDL_malloc(row * rgba->h);
    for (int y = 0; y < rgba->h; ++y) {
      std::memcpy(tight + y * row,
                  (uint8_t*)rgba->pixels + y * rgba->pitch,
                  row);
    }
    uploadRgba8(queue, tex->texture, rgba->w, rgba->h, tight);
    SDL_free(tight);
  }
  SDL_DestroySurface(rgba);
  return tex;
}

Texture* Texture::CreateStreaming(uint32_t width, uint32_t height) {
  auto* engine = Engine::GetInstance();
  if (!engine || !engine->window) {
    SDL_Log("Texture::CreateStreaming called before Window exists");
    return nullptr;
  }
  WGPUDevice device = engine->window->wgpuDevice;

  auto* tex = new Texture();
  tex->dimensions = {(int)width, (int)height};
  tex->texture    = createRgba8Texture(device, width, height, "Texture::Streaming");
  tex->view       = createDefaultView(tex->texture);
  tex->imTextureID = (ImTextureIDStorage)(uintptr_t)tex->view;
  return tex;
}

void Texture::Upload(const void* pixels) {
  auto* engine = Engine::GetInstance();
  if (!engine || !engine->window || !texture) return;
  uploadRgba8(engine->window->wgpuQueue, texture,
              (uint32_t)dimensions.x, (uint32_t)dimensions.y, pixels);
}

void Texture::Draw(Renderer2D& r) {
  Draw(r, position, scale);
}

void Texture::Draw(Renderer2D& r, Vector2f pos, Vector2f scl) {
  Rect2D dst;
  dst.w = dimensions.x * scl.x;
  dst.h = dimensions.y * scl.y;
  dst.x = pos.x - dst.w * 0.5f;   // center, matches old behavior
  dst.y = pos.y - dst.h * 0.5f;
  r.DrawTexture(*this, dst);
}

Texture::~Texture() {
  if (view)    { wgpuTextureViewRelease(view); view = nullptr; }
  if (texture) { wgpuTextureDestroy(texture);
                 wgpuTextureRelease(texture); texture = nullptr; }
}
