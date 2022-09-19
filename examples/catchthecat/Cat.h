#ifndef CAT_H
#define CAT_H

#include "Agent.h"

class Cat: public Agent {
public:
    explicit Cat():Agent(){};
    Point2D Move(World *) override;
};

#endif  // CAT_H
