#include "Catcher.h"
#include "World.h"

Point2D Catcher::Move(World* world) {
  auto side = world->getWorldSideSize()/2;
  for(;;) {
    Point2D p = {Random::Range(-side, side), Random::Range(-side, side)};
    auto cat = world->getCat();
    if(cat.x!=p.x && cat.y!=p.y && !world->getContent(p))
      return p;
  }
}
