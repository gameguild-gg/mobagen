#include <doctest/doctest.h>
#include "render/scene_serialize.hpp"
#include "render/render_bridge.hpp"
#include "scene/transform.hpp"
#include "scene/transform_system.hpp"
#include "ecs/world.hpp"

TEST_CASE("Transform: local-to-world propagation through 3-deep hierarchy") {
  ecs::World w;
  auto root = w.create();
  auto mid = w.create();
  auto leaf = w.create();
  w.add<scene::Transform>(root, glm::vec3(1, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
  w.add<scene::Transform>(mid, glm::vec3(0, 1, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
  w.add<scene::Transform>(leaf, glm::vec3(0, 0, 1), glm::quat(1, 0, 0, 0), glm::vec3(1));
  auto& t_root = w.get<scene::Transform>(root);
  auto& t_mid = w.get<scene::Transform>(mid);
  auto& t_leaf = w.get<scene::Transform>(leaf);
  t_mid.parent = root;
  t_leaf.parent = mid;
  scene::TransformSystem sys;
  sys.rebuild(w);
  sys.update(w);
  auto world_pos = t_leaf.world[3];
  CHECK(world_pos.x == 1.0f);
  CHECK(world_pos.y == 1.0f);
  CHECK(world_pos.z == 1.0f);
}

TEST_CASE("TransformSystem: reparent cascades dirty flag to descendants") {
  ecs::World w;
  auto root = w.create();
  auto child = w.create();
  w.add<scene::Transform>(root, glm::vec3(0, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
  w.add<scene::Transform>(child, glm::vec3(5, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
  auto& t_root = w.get<scene::Transform>(root);
  auto& t_child = w.get<scene::Transform>(child);
  t_child.parent = root;
  scene::TransformSystem sys;
  sys.rebuild(w);
  sys.update(w);
  auto pos_before = t_child.world[3];
  CHECK(pos_before.x == 5.0f);
  t_root.position = glm::vec3(10, 0, 0);
  sys.update(w);
  auto pos_after = t_child.world[3];
  CHECK(pos_after.x == 15.0f);
}

TEST_CASE("Scene serialization: volume resource handles retain their generation") {
  ecs::World source;
  const ecs::Entity entity = source.create();
  source.add<scene::Transform>(entity);

  render::VolumeRenderable volume;
  volume.source.handle = resource::Handle{17u, 42u};
  source.add<render::VolumeRenderable>(entity, volume);

  const std::vector<std::uint8_t> bytes = render::save_scene(source);
  ecs::World restored;
  const std::vector<ecs::Entity> entities =
      render::load_scene(restored, bytes.data(), bytes.size());

  REQUIRE(entities.size() == 1);
  const auto& restored_volume = restored.get<render::VolumeRenderable>(entities[0]);
  CHECK(restored_volume.source.handle == resource::Handle{17u, 42u});
}
