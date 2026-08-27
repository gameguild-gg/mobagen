// Demonstrates the ECS World: entities, components, a two-component "system",
// a tag component, and destroy + index recycling with generation invalidation.

#include "world.hpp"

#include <cstdio>

using namespace ecs;

struct Position {
  float x, y, z;
};
struct Velocity {
  float x, y, z;
};
struct Player {};  // tag (zero-size component)

int main() {
  World w;

  Entity a = w.create();
  w.add<Position>(a, Position{0, 0, 0});
  w.add<Velocity>(a, Velocity{1, 2, 3});

  Entity b = w.create();
  w.add<Position>(b, Position{10, 10, 10});  // no velocity -> skipped by the system

  Entity p = w.create();
  w.add<Position>(p, Position{5, 5, 5});
  w.add<Velocity>(p, Velocity{0, -1, 0});
  w.add<Player>(p, Player{});

  std::printf("created 3 entities, alive=%zu\n\n", w.alive());

  // "Movement system": Position += Velocity for every entity having BOTH.
  const float dt = 1.0f;
  w.view<Position, Velocity>([&](Entity, Position& pos, Velocity& vel) {
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.z += vel.z * dt;
  });

  // Single-component view to print results.
  std::printf("positions after one step:\n");
  w.view<Position>([&](Entity e, Position& pos) {
    std::printf("  idx=%u gen=%u  (%.1f, %.1f, %.1f)%s\n", entity_index(e), entity_gen(e), pos.x, pos.y, pos.z, w.has<Player>(e) ? "  [Player]" : "");
  });

  // Destroy b, then create c — c recycles b's index with a bumped generation,
  // so the old handle b is no longer valid.
  w.destroy(b);
  Entity c = w.create();
  std::printf("\ndestroyed b(idx=%u gen=%u) -> created c(idx=%u gen=%u)\n", entity_index(b), entity_gen(b), entity_index(c), entity_gen(c));
  std::printf("valid(b)=%d  valid(c)=%d  alive=%zu  (recycled index, gen invalidates b)\n", w.valid(b), w.valid(c), w.alive());
  return 0;
}
