#include "Renderer2D.h"
#include "Texture.h"
#include <imgui.h>

namespace {
inline ImU32 packColor(ColorRGBA c) {
  return IM_COL32(c.r, c.g, c.b, c.a);
}
} // namespace

Renderer2D::Renderer2D(ImDrawList* dl, int w, int h)
    : drawList(dl), windowWidthPx(w), windowHeightPx(h) {}

void Renderer2D::SetDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  drawColor = {r, g, b, a};
}

void Renderer2D::DrawPoint(float x, float y) {
  drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + 1, y + 1), packColor(drawColor));
}

void Renderer2D::DrawLine(float x1, float y1, float x2, float y2) {
  drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), packColor(drawColor), 1.0f);
}

void Renderer2D::DrawRect(const Rect2D& r) {
  drawList->AddRect(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h),
                    packColor(drawColor), 0.0f, 0, 1.0f);
}

void Renderer2D::DrawFilledRect(const Rect2D& r) {
  drawList->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h),
                          packColor(drawColor));
}

void Renderer2D::DrawTriangles(const Vertex2D* verts, int vertexCount) {
  // Three verts per triangle. Color = per-vertex (matches SDL_RenderGeometry).
  // We must use TexUvWhitePixel as the UV — ImGui always draws through the font
  // atlas texture, and UV (0,0) lands on a transparent/garbage pixel. The white
  // pixel is the correct UV for solid-colored geometry.
  if (vertexCount < 3) return;
  const int tris = vertexCount / 3;
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  const ImVec2 uvW   = atlas->TexUvWhitePixel;
  // PushTextureID ensures we use the font atlas (with the white pixel) even if
  // a previous AddImage call left a different texture on the stack.
  drawList->PushTextureID(atlas->TexID);
  drawList->PrimReserve(tris * 3, tris * 3);
  for (int t = 0; t < tris; ++t) {
    for (int i = 0; i < 3; ++i) {
      const Vertex2D& v = verts[t * 3 + i];
      drawList->PrimWriteVtx(ImVec2(v.x, v.y), uvW,
                             IM_COL32(v.r, v.g, v.b, v.a));
    }
    drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx - 3));
    drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx - 2));
    drawList->PrimWriteIdx((ImDrawIdx)(drawList->_VtxCurrentIdx - 1));
  }
  drawList->PopTextureID();
}

void Renderer2D::DrawTexture(const Texture& tex, const Rect2D& dst, ColorRGBA tint) {
  ImTextureID id = tex.GetImTextureID();
  if (id == 0) return;
  drawList->AddImage(id,
                     ImVec2(dst.x, dst.y),
                     ImVec2(dst.x + dst.w, dst.y + dst.h),
                     ImVec2(0, 0), ImVec2(1, 1),
                     packColor(tint));
}

void Renderer2D::PushClipRect(const Rect2D& r) {
  drawList->PushClipRect(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), true);
}

void Renderer2D::PopClipRect() {
  drawList->PopClipRect();
}
