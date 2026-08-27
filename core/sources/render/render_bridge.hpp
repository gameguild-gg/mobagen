#pragma once
// ============================================================================
// RenderBridge — DOD scene data -> flat renderer commands.
// ============================================================================
//
// The ECS is good at storing "what exists": entities, components, transforms,
// volume metadata. A GPU renderer is good at consuming a tight list of draw
// commands: "draw this volume with this transform and these display settings."
//
// This bridge is the boundary between those worlds.
//
// Important DOD rule:
//   The ECS stores small metadata and handles. It does NOT store the voxel bytes.
//   A CT volume can be hundreds of MB; that belongs in a loader arena or GPU
//   resource manager. Entities keep a stable id/handle to that resource.

#include "transform.hpp"
#include "world.hpp"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace render {

  enum class VolumeScalarFormat : std::uint8_t {
    UInt8,    // current phantom path: one byte per voxel, normalized in shader
    UInt16,   // common DICOM storage before Hounsfield rescale
    Float16,  // GPU-friendly half float after preprocessing
    Float32
  };

  enum class VolumeRenderMode : std::uint8_t { DVR, MIP, Isosurface };

  struct VolumeSource {
    // Stable resource id. Later this points at a CPU volume asset and/or a GPU
    // texture object. Keeping it as an id makes the C ABI and wasm boundary easy.
    std::uint32_t id = 0;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;

    // DICOM scans are rarely cubic. PixelSpacing and SliceThickness become this.
    // The renderer uses it to scale the volume box so anatomy is not squashed.
    glm::vec3 spacing_mm{1.0f};

    VolumeScalarFormat format = VolumeScalarFormat::UInt8;
  };

  struct VolumeDisplay {
    // Window/level are clinical display controls. They select which intensity
    // range maps to visible colour/opacity before the transfer function.
    float window_center = 0.5f;
    float window_width = 1.0f;

    std::uint32_t transfer_preset = 1;  // current WebGL presets: 1..4
    VolumeRenderMode mode = VolumeRenderMode::DVR;
    float iso_threshold = 0.40f;
  };

  // ECS component: attach this to an entity with scene::Transform to make it
  // renderable as a volume.
  struct VolumeRenderable {
    VolumeSource source;
    VolumeDisplay display;
  };

  // Renderer-facing packet: flat, cache-friendly, no ECS lookup needed while
  // recording GPU commands.
  struct VolumeDrawCommand {
    ecs::Entity entity = ecs::kInvalidEntity;
    glm::mat4 world{1.0f};
    VolumeSource source;
    VolumeDisplay display;
  };

  class RenderBridge {
  public:
    void build(ecs::World& world) {
      volume_commands_.clear();

      // Iterate the smaller Transform pool and gate on VolumeRenderable through
      // the sparse set. The output is a compact array the renderer can stream.
      world.view<scene::Transform, VolumeRenderable>([&](ecs::Entity e, scene::Transform& transform, VolumeRenderable& volume) {
        VolumeDrawCommand cmd;
        cmd.entity = e;
        cmd.world = transform.world;
        cmd.source = volume.source;
        cmd.display = volume.display;
        volume_commands_.push_back(cmd);
      });
    }

    const std::vector<VolumeDrawCommand>& volume_commands() const { return volume_commands_; }

    void clear() { volume_commands_.clear(); }

  private:
    std::vector<VolumeDrawCommand> volume_commands_;
  };

}  // namespace render
