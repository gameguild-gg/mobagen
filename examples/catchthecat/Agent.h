#ifndef AGENT_H
#define AGENT_H
#include "Point2D.h"

class World;

class Agent {
public:
  explicit Agent()= default;;
  virtual Point2D Move(World*)=0;
};

#endif  // AGENT_H
