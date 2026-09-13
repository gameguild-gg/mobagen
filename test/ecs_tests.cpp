#include <doctest/doctest.h>
#include "world.hpp"

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

  world.apply_range<Position, Velocity>(2, 5, [](auto, Position& position, Velocity& velocity) {
    position.x += velocity.vx;
  });

  int dense_index = 0;
  world.view<Position>([&](auto, Position& position) {
    const float expected = dense_index >= 2 && dense_index < 5
                               ? static_cast<float>(dense_index) + 10.0f
                               : static_cast<float>(dense_index);
    CHECK(position.x == expected);
    ++dense_index;
  });
}
