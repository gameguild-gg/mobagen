#include "render_bridge.hpp"
#include "transform_system.hpp"

#include <cstdio>

int main() {
  ecs::World world;
  scene::TransformSystem transforms;

  // A DICOM volume becomes an ECS entity with two small components:
  //   Transform        -> where the volume lives in world space
  //   VolumeRenderable -> which volume resource + how to display it
  //
  // The actual voxel memory is intentionally not stored in the ECS.
  ecs::Entity ct = world.create();

  scene::Transform t;
  t.position = {0.0f, 0.0f, 0.0f};
  t.scale = {1.0f, 1.0f, 1.5f};  // non-cubic z spacing, like many CT stacks
  world.add<scene::Transform>(ct, t);

  render::VolumeRenderable volume;
  volume.source.id = 7;
  volume.source.width = 512;
  volume.source.height = 512;
  volume.source.depth = 300;
  volume.source.spacing_mm = {0.7f, 0.7f, 1.5f};
  volume.source.format = render::VolumeScalarFormat::UInt16;
  volume.display.window_center = 40.0f;
  volume.display.window_width = 400.0f;
  volume.display.transfer_preset = 2;
  volume.display.mode = render::VolumeRenderMode::DVR;
  world.add<render::VolumeRenderable>(ct, volume);

  transforms.rebuild(world);
  transforms.update(world);

  render::RenderBridge bridge;
  bridge.build(world);

  const auto& commands = bridge.volume_commands();
  std::printf("volume draw commands: %zu\n", commands.size());
  if (commands.empty()) return 1;

  const auto& cmd = commands[0];
  std::printf("entity=%llu volume_id=%u dims=%ux%ux%u spacing=(%.2f, %.2f, %.2f) window=(%.1f, %.1f)\n", static_cast<unsigned long long>(cmd.entity),
              cmd.source.id, cmd.source.width, cmd.source.height, cmd.source.depth, cmd.source.spacing_mm.x, cmd.source.spacing_mm.y,
              cmd.source.spacing_mm.z, cmd.display.window_center, cmd.display.window_width);

  return commands.size() == 1 ? 0 : 2;
}
