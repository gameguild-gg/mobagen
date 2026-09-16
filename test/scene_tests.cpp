#include <doctest/doctest.h>
#include "render/scene_serialize.hpp"
#include "render/render_bridge.hpp"
#include "scene/transform.hpp"
#include "scene/transform_system.hpp"
#include "scene/transform_store.hpp"
#include "ecs/world.hpp"

#include <stdexcept>

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
  REQUIRE(sys.rebuild(w));
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
  REQUIRE(sys.rebuild(w));
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
  volume.source.width = 1;
  volume.source.height = 1;
  volume.source.depth = 1;
  source.add<render::VolumeRenderable>(entity, volume);

  const std::vector<std::uint8_t> bytes = render::save_scene(source);
  ecs::World restored;
  const std::vector<ecs::Entity> entities = render::load_scene(restored, bytes.data(), bytes.size());

  REQUIRE(entities.size() == 1);
  const auto& restored_volume = restored.get<render::VolumeRenderable>(entities[0]);
  CHECK(restored_volume.source.handle == resource::Handle{17u, 42u});
}

TEST_CASE("TransformSystem: rebuild rejects self-parenting and cycles") {
  ecs::World world;
  const ecs::Entity first = world.create();
  const ecs::Entity second = world.create();
  world.add<scene::Transform>(first);
  world.add<scene::Transform>(second);
  scene::TransformSystem system;

  world.get<scene::Transform>(first).parent = first;
  CHECK_FALSE(system.rebuild(world));

  world.get<scene::Transform>(first).parent = second;
  world.get<scene::Transform>(second).parent = first;
  CHECK_FALSE(system.rebuild(world));
}

TEST_CASE("TransformSystem: rebuild rejects stale and component-less parents") {
  scene::TransformSystem system;

  ecs::World stale_world;
  const ecs::Entity stale_parent = stale_world.create();
  const ecs::Entity stale_child = stale_world.create();
  stale_world.add<scene::Transform>(stale_parent);
  stale_world.add<scene::Transform>(stale_child).parent = stale_parent;
  stale_world.destroy(stale_parent);
  CHECK_FALSE(system.rebuild(stale_world));

  ecs::World missing_world;
  const ecs::Entity missing_parent = missing_world.create();
  const ecs::Entity missing_child = missing_world.create();
  missing_world.add<scene::Transform>(missing_child).parent = missing_parent;
  CHECK_FALSE(system.rebuild(missing_world));
}

TEST_CASE("TransformSystem: a failed rebuild preserves the previous frozen order") {
  ecs::World world;
  const ecs::Entity root = world.create();
  const ecs::Entity child = world.create();
  world.add<scene::Transform>(root).position = {1.0f, 0.0f, 0.0f};
  world.add<scene::Transform>(child).parent = root;

  scene::TransformSystem system;
  REQUIRE(system.rebuild(world));
  REQUIRE(system.size() == 2);

  world.get<scene::Transform>(root).parent = child;
  CHECK_FALSE(system.rebuild(world));
  CHECK(system.size() == 2);

  world.get<scene::Transform>(root).parent = ecs::kInvalidEntity;
  world.get<scene::Transform>(root).position = {3.0f, 0.0f, 0.0f};
  world.get<scene::Transform>(child).position = {2.0f, 0.0f, 0.0f};
  system.update(world);
  CHECK(world.get<scene::Transform>(child).world[3].x == 5.0f);
}

TEST_CASE("TransformSystem: update skips stale slots and their replacements") {
  ecs::World world;
  const ecs::Entity stale = world.create();
  world.add<scene::Transform>(stale).position = {1.0f, 0.0f, 0.0f};
  scene::TransformSystem system;
  REQUIRE(system.rebuild(world));

  world.destroy(stale);
  const ecs::Entity replacement = world.create();
  REQUIRE(ecs::entity_index(replacement) == ecs::entity_index(stale));
  auto& replacement_transform = world.add<scene::Transform>(replacement);
  replacement_transform.position = {9.0f, 0.0f, 0.0f};

  CHECK_NOTHROW(system.update(world));
  CHECK(replacement_transform.world[3].x == 0.0f);
}

TEST_CASE("TransformStore: create rejects parents that are not already present") {
  scene::TransformStore store;
  CHECK_THROWS_AS(store.create(0), std::out_of_range);
  CHECK(store.size() == 0);

  const scene::TransformStore::Id root = store.create();
  CHECK_THROWS_AS(store.create(1), std::out_of_range);
  CHECK_THROWS_AS(store.create(scene::TransformStore::npos - 1), std::out_of_range);
  CHECK(store.size() == 1);

  const scene::TransformStore::Id child = store.create(root);
  CHECK(child == 1);
  CHECK(store.size() == 2);
}
