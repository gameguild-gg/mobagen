#ifndef AGENT_H
#define AGENT_H
#include "Point2D.h"

class World;

class Agent {
public:
  explicit Agent(World *world):world(world){};

protected:
  World *world;

private:
  virtual bool moveIsAcceptable(Point2D& p)=0;
};

#endif  // AGENT_H
