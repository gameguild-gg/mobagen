#include "Catcher.h"
#include "World.h"

Catcher::Catcher(World* world) : Agent(world) {}

bool Catcher::moveIsAcceptable(Point2D& p) { return false; }
