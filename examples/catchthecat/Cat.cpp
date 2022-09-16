#include "Cat.h"
#include "World.h"

bool Cat::moveIsAcceptable(Point2D &p) {
    if(world->isValidPosition(p))
        return true;
    return false;
}
