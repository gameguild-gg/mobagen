// ============================================================================
// dod_engine_slice — the editor's core loop in miniature (headless).
// ============================================================================
// Composes the greenfield DOD capabilities the way the editor will:
//   resource registry (asset by handle) -> ECS scene (Transform + VolumeRenderable)
//   -> input mutates the scene -> RenderBridge flattens to draw commands
//   -> save/load round-trips the scene.
// Proves the modules work together, ahead of the actual editor port. Native
// toolchain is unavailable, so build with em++ + run under node:
//   em++ -std=c++20 apps/core_demos/sources/composition/dod_engine_slice.cpp \
//        -I core/sources/ecs -I core/sources/scene -I core/sources/render \
//        -I core/sources/input -I core/sources/resource -I <glm> -o build/slice.js
//   node build/slice.js
#include "world.hpp"
#include "transform.hpp"
#include "render_bridge.hpp"
#include "scene_serialize.hpp"
#include "input_state.hpp"
#include "resource_registry.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  // 1) Resource store owns the heavy asset; the entity refers to it by handle.
  //    (int payload here stands in for a volume::VolumeBuffer.)
  resource::ResourceRegistry<int> assets;
  auto volAsset = assets.create(96 * 96 * 96);
  assert(assets.get(volAsset) && *assets.get(volAsset) == 96 * 96 * 96);

  // 2) Author a scene: one volume entity (Transform + VolumeRenderable).
  ecs::World world;
  ecs::Entity e = world.create();
  world.add<scene::Transform>(e, scene::Transform{});
  render::VolumeRenderable v;
  v.source.id = volAsset.index;  // handle into the resource registry
  v.source.width = v.source.height = v.source.depth = 96;
  v.source.spacing_mm = {1.0f, 1.0f, 1.5f};
  v.display.mode = render::VolumeRenderMode::DVR;
  world.add<render::VolumeRenderable>(e, v);

  // 3) Input -> mutate the scene (editor interaction: drag yaws the volume).
  input::InputState in;
  in.begin_frame();
  in.on_mouse_button(0, true);
  in.on_mouse_move(10.0f, 10.0f, 8.0f, 0.0f);  // drag right by 8px
  if (in.mouse_held(0) && std::fabs(in.mouse_dx()) > 0.0f) {
    const float yaw = in.mouse_dx() * 0.01f;
    auto& tr = world.get<scene::Transform>(e);
    tr.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * tr.rotation;
    tr.dirty = true;
  }
  const glm::quat rotated = world.get<scene::Transform>(e).rotation;
  assert(std::fabs(rotated.w - 1.0f) > 1e-6f && "drag should have rotated the volume");

  // 4) RenderBridge: ECS -> flat, GPU-ready draw commands.
  render::RenderBridge bridge;
  bridge.build(world);
  assert(bridge.volume_commands().size() == 1);
  assert(bridge.volume_commands()[0].source.id == volAsset.index);

  // 5) Persist + reload the scene (editor save/open).
  std::vector<std::uint8_t> blob = render::save_scene(world);
  ecs::World reopened;
  std::vector<ecs::Entity> restored = render::load_scene(reopened, blob.data(), blob.size());
  assert(restored.size() == 1);
  assert(reopened.has<render::VolumeRenderable>(restored[0]));
  assert(reopened.get<render::VolumeRenderable>(restored[0]).source.depth == 96);

  // 6) Releasing the asset invalidates the handle (use-after-free safety).
  assets.release(volAsset);
  assert(assets.get(volAsset) == nullptr);

  std::printf(
      "dod_engine_slice OK: resource handle -> scene -> input mutation -> "
      "render bridge (%zu cmd) -> save/load (%zu bytes) composed\n",
      bridge.volume_commands().size(), blob.size());
  return 0;
}
