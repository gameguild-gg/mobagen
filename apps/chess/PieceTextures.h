#ifndef CHESS_PIECETEXTURES_H
#define CHESS_PIECETEXTURES_H

#include "WorldState.h"
#include "imgui.h"
#include <webgpu/webgpu.h>

// Rasterizes the 12 embedded piece SVGs (pieces/PieceSvg.h) into WGPU
// textures once at startup. Manager::OnDraw blits them through the ImGui
// background draw list; the backend treats a WGPUTextureView as ImTextureID.
class PieceTextures {
public:
  // device must outlive this object (views only reference-count the textures).
  bool load(WGPUDevice device);

  ImTextureID texture(PieceData piece) const;
  bool ready() const { return loaded; }

private:
  static constexpr int kTextureSize = 256;  // also keeps bytesPerRow 256-byte aligned

  bool loaded = false;
  ImTextureID ids[12] = {};
  WGPUTexture textures[12] = {};
  WGPUTextureView views[12] = {};
};

#endif  // CHESS_PIECETEXTURES_H
