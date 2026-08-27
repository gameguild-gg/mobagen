// Scene demo: a root -> child -> grandchild hierarchy built as ECS entities with
// Transform components. The TransformSystem resolves world matrices; moving the
// root propagates to the whole subtree.

#include "transform.hpp"
#include "transform_system.hpp"
#include "world.hpp"

#include <cstdio>

using ecs::Entity;
using ecs::World;
using scene::Transform;
using scene::TransformSystem;

static void print_pos(const char* name, const glm::mat4& m) {
  std::printf("  %-5s world pos = (%.1f, %.1f, %.1f)\n", name, m[3][0], m[3][1], m[3][2]);
}

int main() {
  World w;
  TransformSystem ts;

  Entity root = w.create();
  {
    Transform t;
    t.position = {10, 0, 0};
    w.add<Transform>(root, t);
  }
  Entity child = w.create();
  {
    Transform t;
    t.position = {0, 5, 0};
    t.parent = root;
    w.add<Transform>(child, t);
  }
  Entity grand = w.create();
  {
    Transform t;
    t.position = {0, 0, 2};
    t.parent = child;
    w.add<Transform>(grand, t);
  }

  ts.rebuild(w);  // build the flat parents-before-children order once
  ts.update(w);
  std::printf("built (root @10,0,0 ; child +0,5,0 ; grand +0,0,2):\n");
  print_pos("root", w.get<Transform>(root).world);
  print_pos("child", w.get<Transform>(child).world);
  print_pos("grand", w.get<Transform>(grand).world);

  w.get<Transform>(root).position = {20, 0, 0};
  ts.update(w);
  std::printf("after moving root to (20,0,0) -> subtree follows:\n");
  print_pos("root", w.get<Transform>(root).world);
  print_pos("child", w.get<Transform>(child).world);
  print_pos("grand", w.get<Transform>(grand).world);
  return 0;
}
