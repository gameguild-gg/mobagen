#ifndef AGENT_H
#define AGENT_H
#include "math/Point2D.h"

#include <unordered_set>
#include <vector>

class World;

class Agent {
public:
  explicit Agent() = default;

  virtual Point2D Move(World*) = 0;

  std::vector<Point2D> getVisitableNeighbors(World* world, const Point2D& current, std::unordered_set<Point2D>& visited);
  std::vector<Point2D> generatePath(World* w);
};

#endif  // AGENT_H
