#ifndef CATCHER_H
#define CATCHER_H

#include "Agent.h"

class Catcher: public Agent {
 public:
    explicit Catcher(World *world);
 private:
    bool moveIsAcceptable(Point2D& p) override;
};

#endif  // CATCHER_H
