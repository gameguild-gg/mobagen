#include "PieceTextures.h"
#include "pieces/PieceSvg.h"

#include <lunasvg.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace {
  // Index layout: [color][type-1], types are King..Pawn (1..6).
  int pieceIndex(PieceColor color, PieceType type) {
    const int base = color == PieceColor::White ? 0 : 6;
    const int kind = (static_cast<int>(type) & static_cast<int>(PieceType::PIECEMASK)) - 1;
    if (kind < 0 || kind > 5) return -1;
    return base + kind;
  }
}  // namespace

bool PieceTextures::load(WGPUDevice device) {
  static const std::string* svgs[12] = {
      &KingSvgWhite, &QueenSvgWhite, &BishopSvgWhite, &KnightSvgWhite, &RookSvgWhite, &PawnSvgWhite,
      &KingSvgBlack, &QueenSvgBlack, &BishopSvgBlack, &KnightSvgBlack, &RookSvgBlack, &PawnSvgBlack,
  };

  WGPUQueue queue = wgpuDeviceGetQueue(device);

  for (int i = 0; i < 12; ++i) {
    auto document = lunasvg::Document::loadFromData(*svgs[i]);
    if (!document) {
      SDL_Log("chess: piece SVG %d failed to parse", i);
      return false;
    }

    lunasvg::Bitmap bitmap = document->renderToBitmap(kTextureSize, kTextureSize);
    bitmap.convertToRGBA();
    if (!bitmap.data()) {
      SDL_Log("chess: piece SVG %d failed to rasterize", i);
      return false;
    }

    WGPUTextureDescriptor tex_desc = {};
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size = {static_cast<uint32_t>(kTextureSize), static_cast<uint32_t>(kTextureSize), 1};
    tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    textures[i] = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!textures[i]) return false;

    WGPUTexelCopyTextureInfo copy_dst = {};
    copy_dst.texture = textures[i];
    copy_dst.mipLevel = 0;
    copy_dst.origin = {0, 0, 0};
    copy_dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = kTextureSize * 4;
    layout.rowsPerImage = kTextureSize;

    WGPUExtent3D extent = {static_cast<uint32_t>(kTextureSize), static_cast<uint32_t>(kTextureSize), 1};
    wgpuQueueWriteTexture(queue, &copy_dst, bitmap.data(), kTextureSize * kTextureSize * 4, &layout, &extent);

    WGPUTextureViewDescriptor view_desc = {};
    view_desc.format = tex_desc.format;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.baseMipLevel = 0;
    view_desc.mipLevelCount = 1;
    view_desc.baseArrayLayer = 0;
    view_desc.arrayLayerCount = 1;
    view_desc.aspect = WGPUTextureAspect_All;
    views[i] = wgpuTextureCreateView(textures[i], &view_desc);
    if (!views[i]) return false;

    ids[i] = reinterpret_cast<ImTextureID>(views[i]);
  }

  loaded = true;
  return true;
}

ImTextureID PieceTextures::texture(PieceData piece) const {
  if (!loaded) return 0;
  const int i = pieceIndex(piece.Color(), piece.Piece());
  if (i < 0) return 0;
  return ids[i];
}
