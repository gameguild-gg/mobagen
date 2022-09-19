#ifndef CAT_H
#define CAT_H

#include "Agent.h"

class Cat: public Agent {
public:
    explicit Cat():Agent(){};
    Point2D Move(const World *) override;
};

#endif  // CAT_H
