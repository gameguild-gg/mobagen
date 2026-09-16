#include <doctest/doctest.h>
#include "group.hpp"
#include "world.hpp"

#include <atomic>
#include <stdexcept>
#include <thread>

namespace {
  struct Position {
    float x = 0, y = 0, z = 0;
    Position() = default;
    Position(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  };
  struct Velocity {
    float vx = 0, vy = 0;
    Velocity() = default;
    Velocity(float vx_, float vy_) : vx(vx_), vy(vy_) {}
  };
}  // namespace

TEST_CASE("World: create entity and check generation") {
  ecs::World w;
  auto e0 = w.create();
  CHECK(w.valid(e0));
  auto e1 = w.create();
  CHECK(w.valid(e1));
  CHECK(e0 != e1);
  w.destroy(e0);
  CHECK(!w.valid(e0));
  auto e2 = w.create();
  CHECK(w.valid(e2));
}

TEST_CASE("World: a destroyed slot is invalid until it is recycled") {
  ecs::World world;
  const ecs::Entity entity = world.create();
  world.destroy(entity);

  const ecs::Entity fabricated_current_generation = ecs::make_entity(ecs::entity_index(entity), ecs::entity_gen(entity) + 1);

  CHECK_FALSE(world.valid(fabricated_current_generation));
}

TEST_CASE("World: stale handles cannot observe a recycled entity's components") {
  ecs::World world;
  const ecs::Entity stale = world.create();
  world.add<Position>(stale, 1.0f, 2.0f, 3.0f);
  world.destroy(stale);

  const ecs::Entity replacement = world.create();
  REQUIRE(ecs::entity_index(replacement) == ecs::entity_index(stale));
  world.add<Position>(replacement, 4.0f, 5.0f, 6.0f);

  CHECK_FALSE(world.has<Position>(stale));
  CHECK(world.has<Position>(replacement));
}

TEST_CASE("World: stale handles cannot attach components to a replacement") {
  ecs::World world;
  const ecs::Entity stale = world.create();
  world.destroy(stale);
  const ecs::Entity replacement = world.create();

  CHECK_THROWS_AS(world.add<Velocity>(stale, 1.0f, 2.0f), std::invalid_argument);
  CHECK_FALSE(world.has<Velocity>(replacement));
}

TEST_CASE("World: duplicate components are rejected without changing storage") {
  ecs::World world;
  const ecs::Entity entity = world.create();
  world.add<Position>(entity, 1.0f, 2.0f, 3.0f);

  CHECK_THROWS_AS(world.add<Position>(entity, 4.0f, 5.0f, 6.0f), std::logic_error);
  CHECK(world.count<Position>() == 1);
  CHECK(world.get<Position>(entity).x == 1.0f);
}

TEST_CASE("World: missing components have explicit safe operations") {
  ecs::World world;
  const ecs::Entity entity = world.create();

  CHECK(world.try_get<Position>(entity) == nullptr);
  CHECK_THROWS_AS(world.get<Position>(entity), std::out_of_range);
  CHECK_FALSE(world.remove<Position>(entity));

  world.add<Position>(entity, 1.0f, 2.0f, 3.0f);
  const ecs::World& const_world = world;
  REQUIRE(const_world.try_get<Position>(entity) != nullptr);
  CHECK(const_world.get<Position>(entity).x == 1.0f);
  CHECK(world.remove<Position>(entity));
  CHECK_FALSE(world.remove<Position>(entity));
}

TEST_CASE("World: destroy reports whether an entity was alive") {
  ecs::World world;
  const ecs::Entity entity = world.create();

  CHECK(world.destroy(entity));
  CHECK_FALSE(world.destroy(entity));
}

TEST_CASE("World: views tolerate absent storage and ranges reject invalid bounds") {
  ecs::World world;
  const ecs::Entity entity = world.create();
  int calls = 0;

  world.view<Position>([&](auto, Position&) { ++calls; });
  world.apply_range<Position, Velocity>(0, 0, [&](auto, Position&, Velocity&) { ++calls; });
  CHECK(calls == 0);

  world.add<Position>(entity, 1.0f, 2.0f, 3.0f);
  const auto past_end = [&] { world.apply_range<Position, Velocity>(0, 2, [](auto, Position&, Velocity&) {}); };
  const auto reversed = [&] { world.apply_range<Position, Velocity>(1, 0, [](auto, Position&, Velocity&) {}); };
  CHECK_THROWS_AS(past_end(), std::out_of_range);
  CHECK_THROWS_AS(reversed(), std::out_of_range);
}

TEST_CASE("Storage: add component, get, has, remove") {
  ecs::World w;
  auto e = w.create();
  w.add<Position>(e, 1.0f, 2.0f, 3.0f);
  CHECK(w.has<Position>(e));
  auto& p = w.get<Position>(e);
  CHECK(p.x == 1.0f);
  CHECK(p.y == 2.0f);
  CHECK(p.z == 3.0f);
  w.add<Velocity>(e, 4.0f, 5.0f);
  CHECK(w.has<Velocity>(e));
  w.remove<Velocity>(e);
  CHECK(!w.has<Velocity>(e));
  CHECK(w.has<Position>(e));
}

TEST_CASE("View: iterates exactly matching entities") {
  ecs::World w;
  auto e0 = w.create();
  auto e1 = w.create();
  auto e2 = w.create();
  w.add<Position>(e0, 1.0f, 0.0f, 0.0f);
  w.add<Position>(e1, 2.0f, 0.0f, 0.0f);
  w.add<Velocity>(e1, 1.0f, 0.0f);
  w.add<Position>(e2, 3.0f, 0.0f, 0.0f);
  w.add<Velocity>(e2, 2.0f, 0.0f);
  int count = 0;
  w.view<Position, Velocity>([&](auto, Position&, Velocity&) { ++count; });
  CHECK(count == 2);
  int countPos = 0;
  w.view<Position>([&](auto, Position&) { ++countPos; });
  CHECK(countPos == 3);
}

TEST_CASE("World: type-erased destroy cleans all components") {
  ecs::World w;
  auto e = w.create();
  w.add<Position>(e, 1.0f, 2.0f, 3.0f);
  w.add<Velocity>(e, 4.0f, 5.0f);
  w.destroy(e);
  CHECK(!w.has<Position>(e));
  CHECK(!w.has<Velocity>(e));
}

TEST_CASE("World: apply_range updates only the requested dense interval") {
  ecs::World world;
  for (int i = 0; i < 8; ++i) {
    const auto entity = world.create();
    world.add<Position>(entity, static_cast<float>(i), 0.0f, 0.0f);
    world.add<Velocity>(entity, 10.0f, 0.0f);
  }

  world.apply_range<Position, Velocity>(2, 5, [](auto, Position& position, Velocity& velocity) { position.x += velocity.vx; });

  int dense_index = 0;
  world.view<Position>([&](auto, Position& position) {
    const float expected = dense_index >= 2 && dense_index < 5 ? static_cast<float>(dense_index) + 10.0f : static_cast<float>(dense_index);
    CHECK(position.x == expected);
    ++dense_index;
  });
}

TEST_CASE("World: structural mutation is rejected away from the owner thread") {
  ecs::World world;
  const ecs::Entity entity = world.create();
  world.add<Position>(entity, 1.0f, 2.0f, 3.0f);
  ecs::Group<Position, Velocity> group(world);
  std::atomic<int> rejected{0};

  std::thread worker([&] {
    try {
      (void)world.create();
    } catch (const std::logic_error&) {
      rejected.fetch_add(1, std::memory_order_relaxed);
    }
    try {
      world.add<Velocity>(entity, 1.0f, 2.0f);
    } catch (const std::logic_error&) {
      rejected.fetch_add(1, std::memory_order_relaxed);
    }
    try {
      (void)world.remove<Position>(entity);
    } catch (const std::logic_error&) {
      rejected.fetch_add(1, std::memory_order_relaxed);
    }
    try {
      (void)world.destroy(entity);
    } catch (const std::logic_error&) {
      rejected.fetch_add(1, std::memory_order_relaxed);
    }
    try {
      group.refresh();
    } catch (const std::logic_error&) {
      rejected.fetch_add(1, std::memory_order_relaxed);
    }
  });
  worker.join();

  CHECK(rejected.load(std::memory_order_relaxed) == 5);
  CHECK(world.alive() == 1);
  CHECK(world.valid(entity));
  CHECK(world.has<Position>(entity));
  CHECK_FALSE(world.has<Velocity>(entity));
}

TEST_CASE("World: workers may update disjoint pre-existing component ranges") {
  ecs::World world;
  for (int i = 0; i < 8; ++i) {
    const ecs::Entity entity = world.create();
    world.add<Position>(entity, static_cast<float>(i), 0.0f, 0.0f);
    world.add<Velocity>(entity, 10.0f, 0.0f);
  }

  auto update = [&](std::size_t begin, std::size_t end) {
    world.apply_range<Position, Velocity>(begin, end, [](auto, Position& position, Velocity& velocity) { position.x += velocity.vx; });
  };
  std::thread first(update, 0, 4);
  std::thread second(update, 4, 8);
  first.join();
  second.join();

  int dense_index = 0;
  world.view<Position>([&](auto, Position& position) {
    CHECK(position.x == static_cast<float>(dense_index) + 10.0f);
    ++dense_index;
  });
}
