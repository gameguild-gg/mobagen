#include "Catcher.h"
#include "World.h"

Point2D Catcher::Move(World* world) {
  auto path = Agent::generatePath(world);
  if (!path.empty()) {
    return path.front();
  }
  else {
    auto side = world->getWorldSideSize() / 2;
    auto neighs = world->neighbors(world->getCat()); // take a list of all the positions around the cat
    for (int i = 0; i < neighs.size(); i++) {
      if (!world->catcherCanMoveToPosition(neighs[i])) {
      neighs.erase(neighs.begin() + i); // remove the invalids
      }
    }
    if (!neighs.empty()) {
    int dir = Random::Range(0, neighs.size()-1);
      return neighs[dir];
    }
    /*for (;;) {
      Point2D p = {Random::Range(-side, side), Random::Range(-side, side)};
      auto cat = world->getCat();
      if (cat.x != p.x && cat.y != p.y && !world->getContent(p)) return p;
    } */
  }
}
