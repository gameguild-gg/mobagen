#pragma once

// TODO: This is a temporary implementation based on IMGUI's draw list API, to avoid rewriting all the old demos at once! we weed to improve that later.

#include "math/Vector2.h"
#include <cstdint>

struct ImDrawList;
class  Texture;

// SDL3 vertex (matches `SDL_Vertex` layout: float2 pos, FColor, float2 tex).
// We don't include <SDL3/SDL.h> from headers; demos that already include SDL3
// can pass `SDL_Vertex*` reinterpret_cast<const Vertex2D*> if needed, but the
// canonical type for new code is this one.
struct Vertex2D {
  float x, y;       // position in pixels (window space)
  uint8_t r, g, b, a;
  float u, v;       // texture coords (0..1); ignored when no texture
};

struct Rect2D {
  float x, y;
  float w, h;
};

struct ColorRGBA {
  uint8_t r, g, b, a;
};

// 2D draw API that the engine hands to game objects each frame.
//
// Implementation note: this is currently a thin wrapper around an ImGui
// `ImDrawList*` (the background draw list). That means it draws *behind* any
// ImGui windows, matching the old SDL_Renderer behavior. The interface is
// intentionally close to SDL_Renderer so demos translate 1:1; later it can be
// reimplemented natively on WebGPU without rewriting any demo code.
class Renderer2D {
public:
  // Engine constructs one per frame, bound to ImGui::GetBackgroundDrawList().
  explicit Renderer2D(ImDrawList* drawList, int windowWidthPx, int windowHeightPx);

  // ---- state -----------------------------------------------------------
  void SetDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
  void SetDrawColor(ColorRGBA c) { SetDrawColor(c.r, c.g, c.b, c.a); }
  ColorRGBA GetDrawColor() const { return drawColor; }

  // ---- primitives (current color) -------------------------------------
  void DrawPoint(float x, float y);
  void DrawLine (float x1, float y1, float x2, float y2);
  void DrawRect (const Rect2D& r);          // outline
  void DrawFilledRect(const Rect2D& r);     // filled

  // Per-vertex colored triangles (every 3 verts is one triangle).
  void DrawTriangles(const Vertex2D* verts, int vertexCount);

  // ---- textures --------------------------------------------------------
  // Draws `tex` stretched into `dst`, modulated by the supplied tint
  // (defaults to opaque white = no tint).
  void DrawTexture(const Texture& tex, const Rect2D& dst,
                   ColorRGBA tint = {255, 255, 255, 255});

  // ---- clipping --------------------------------------------------------
  void PushClipRect(const Rect2D& r);
  void PopClipRect();

  // ---- accessors -------------------------------------------------------
  int  WindowWidth()  const { return windowWidthPx;  }
  int  WindowHeight() const { return windowHeightPx; }
  ImDrawList* GetImDrawList() const { return drawList; }

private:
  ImDrawList* drawList;
  int         windowWidthPx;
  int         windowHeightPx;
  ColorRGBA   drawColor{255, 255, 255, 255};
};
