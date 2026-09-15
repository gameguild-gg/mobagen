#include "world.hpp"

int main() {
  ecs::World world;
  const auto entity = world.create();
  return world.valid(entity) ? 0 : 1;
}
