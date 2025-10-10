#include "Cat.h"
#include "World.h"
#include <stdexcept>
#include <vector>

Point2D Cat::Move(World* world) {
  auto path = Agent::generatePath(world);
  if (!path.empty()) {
    return path.back();
  }
  else {
    auto pos = world->getCat();
    auto valid = world->neighbors(pos);
    for (int i = 0; i < valid.size(); i++) {
      if (!world->catCanMoveToPosition(valid[i])) {
        valid.erase(valid.begin() + i);
      }
    } // create a list of all 6 neighbors, then call random inside that range
    if (!valid.empty()) {
      int dir = Random::Range(0, valid.size()-1);
      do {
        dir = Random::Range(0, valid.size()-1);
      }while (!world->catCanMoveToPosition(valid[dir]));
      return valid[dir];
    }

  }
}