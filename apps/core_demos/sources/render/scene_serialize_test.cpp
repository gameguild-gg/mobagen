// Round-trip test for render::save_scene / load_scene.
// Native toolchain is currently unavailable, so this is built with em++ and run
// under node:
//   em++ -std=c++20 apps/core_demos/sources/render/scene_serialize_test.cpp \
//        -I core/sources/ecs -I core/sources/scene -I core/sources/render \
//        -I <glm-include> -o build/serialize_test.js && node build/serialize_test.js
#include "scene_serialize.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  ecs::World w;

  ecs::Entity volEnt = w.create();
  scene::Transform t;
  t.position = {1.0f, 2.0f, 3.0f};
  t.scale = {0.5f, 0.5f, 0.75f};
  w.add<scene::Transform>(volEnt, t);

  render::VolumeRenderable v;
  v.source.id = 7;
  v.source.width = 96;
  v.source.height = 96;
  v.source.depth = 96;
  v.source.spacing_mm = {1.0f, 1.0f, 1.5f};
  v.source.format = render::VolumeScalarFormat::UInt16;
  v.display.window_center = 1064.0f;  // brain window in stored units (40 HU + 1024)
  v.display.window_width = 400.0f;
  v.display.transfer_preset = 2;
  v.display.mode = render::VolumeRenderMode::DVR;
  w.add<render::VolumeRenderable>(volEnt, v);

  // A second, child entity with only a Transform (exercises parent remap).
  ecs::Entity child = w.create();
  scene::Transform ct;
  ct.position = {-4.0f, 0.0f, 0.0f};
  ct.parent = volEnt;
  w.add<scene::Transform>(child, ct);

  std::vector<std::uint8_t> bytes = render::save_scene(w);

  ecs::World w2;
  std::vector<ecs::Entity> created = render::load_scene(w2, bytes.data(), bytes.size());
  assert(created.size() == 2 && "expected two entities restored");

  // Find the volume entity among the restored set (order matches save order).
  bool foundVolume = false, foundChildParent = false;
  for (std::size_t i = 0; i < created.size(); ++i) {
    ecs::Entity e = created[i];
    assert(w2.has<scene::Transform>(e));
    if (w2.has<render::VolumeRenderable>(e)) {
      foundVolume = true;
      const auto& t2 = w2.get<scene::Transform>(e);
      const auto& v2 = w2.get<render::VolumeRenderable>(e);
      assert(std::fabs(t2.position.x - 1.0f) < 1e-6f);
      assert(std::fabs(t2.scale.z - 0.75f) < 1e-6f);
      assert(v2.source.width == 96 && v2.source.depth == 96);
      assert(v2.source.format == render::VolumeScalarFormat::UInt16);
      assert(std::fabs(v2.source.spacing_mm.z - 1.5f) < 1e-6f);
      assert(std::fabs(v2.display.window_center - 1064.0f) < 1e-3f);
      assert(v2.display.transfer_preset == 2u);
      assert(v2.display.mode == render::VolumeRenderMode::DVR);
    } else {
      const auto& ct2 = w2.get<scene::Transform>(e);
      if (w2.valid(ct2.parent)) foundChildParent = true;
      assert(std::fabs(ct2.position.x + 4.0f) < 1e-6f);
    }
  }
  assert(foundVolume && "volume renderable not restored");
  assert(foundChildParent && "child parent handle not remapped to a valid entity");

  std::printf(
      "scene_serialize round-trip OK: %zu bytes, 2 entities, "
      "Transform + VolumeRenderable + parent remap verified\n",
      bytes.size());
  return 0;
}
