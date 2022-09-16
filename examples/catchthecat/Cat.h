#ifndef CAT_H
#define CAT_H

#include "Agent.h"

class Cat: public Agent {
public:
    explicit Cat(World *world):Agent(world){};
    bool moveIsAcceptable(Point2D& p) override;
};

#endif  // CAT_H
