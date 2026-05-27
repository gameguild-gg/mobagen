#pragma once

#include "math/Point2D.h"
#include "math/Vector2.h"
#include <cstddef>
#include <cstdint>
#include <string>

// ImGui's ImTextureID is `void*` in normal builds. We avoid pulling in imgui.h
// here by using a uint64_t alias matching its actual size on all targets we
// support; the cpp file casts between them.
using ImTextureIDStorage = unsigned long long;

class Renderer2D;
struct Rect2D;
struct ColorRGBA;

// WebGPU forward decls.
struct WGPUTextureImpl;     typedef WGPUTextureImpl*     WGPUTexture;
struct WGPUTextureViewImpl; typedef WGPUTextureViewImpl* WGPUTextureView;

// A 2D RGBA8 texture backed by a WebGPU `WGPUTexture` + view. The view is
// what ImGui's WebGPU backend uses as ImTextureID.
class Texture {
public:
  Point2D  dimensions{0, 0};      // pixels
  Vector2f scale   {1.f, 1.f};
  Vector2f position{0.f, 0.f};

  Texture() = default;
  ~Texture();

  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  // Decode an SVG (or PNG/JPG — SDL3_image handles any format it knows) from
  // an in-memory buffer and upload as an RGBA8 texture.
  static Texture* LoadSVGFromString(const std::string& svgtxt);

  // Create an empty texture (RGBA8) suitable for per-frame CPU updates via
  // Upload(). Used by the `scenario` demo.
  static Texture* CreateStreaming(uint32_t width, uint32_t height);

  // Upload `width * height * 4` bytes (RGBA8) from `pixels`. The buffer size
  // must match the texture's full dimensions.
  void Upload(const void* pixels);

  // Convenience draws (legacy demos that hold a Texture and a Renderer2D).
  void Draw(Renderer2D& r);
  void Draw(Renderer2D& r, Vector2f pos, Vector2f scl);

  // For Renderer2D internal use — returns the WGPU texture view cast to
  // ImTextureID's underlying type.
  ImTextureIDStorage GetImTextureID() const { return imTextureID; }

  uint32_t Width()  const { return (uint32_t)dimensions.x; }
  uint32_t Height() const { return (uint32_t)dimensions.y; }

private:
  WGPUTexture        texture     = nullptr;
  WGPUTextureView    view        = nullptr;
  ImTextureIDStorage imTextureID = 0;
};
