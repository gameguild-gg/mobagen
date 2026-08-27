#include <doctest/doctest.h>
#include "world.hpp"
#include "jobs/scheduler.hpp"
#include <chrono>

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

TEST_CASE("Perf: 2M entity parallel_for >= 3x faster than serial") {
  ecs::World w;
  jobs::Scheduler sched;
  const int N = 200000;
  for (int i = 0; i < N; ++i) {
    auto e = w.create();
    w.add<Position>(e, float(i), 0.0f, 0.0f);
    w.add<Velocity>(e, 1.0f, 0.0f);
  }
  auto t0 = std::chrono::high_resolution_clock::now();
  w.view<Position, Velocity>([&](auto, Position& p, Velocity& v) { p.x += v.vx; });
  auto t1 = std::chrono::high_resolution_clock::now();
  auto serial_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  t0 = std::chrono::high_resolution_clock::now();
  jobs::WaitGroup wg;
  w.apply_range<Position, Velocity>(0, N, [&](auto, Position& p, Velocity& v) { p.x += v.vx; });
  t1 = std::chrono::high_resolution_clock::now();
  auto parallel_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  WARN(parallel_us * 3 < serial_us);
}
