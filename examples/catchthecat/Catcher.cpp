#include "Catcher.h"
#include "World.h"

Point2D Catcher::Move(World* world) {
  for(;;) {
    Point2D p = {Random::Range(-5, 5), Random::Range(-5, 5)};
    if(world->getCat()!=p && !world->getContent(p))
      return p;
  }
}
