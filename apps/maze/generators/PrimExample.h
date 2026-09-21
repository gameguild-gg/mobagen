#ifndef MOBAGEN_EXAMPLES_MAZE_GENERATORS_PRIMEXAMPLE_H_
#define MOBAGEN_EXAMPLES_MAZE_GENERATORS_PRIMEXAMPLE_H_

#include <vector>
#include <string>
#include "../MazeGeneratorBase.h"
#include "math/Point2D.h"
#include <map>

class PrimExample : public MazeGeneratorBase {
private:
  std::vector<Point2D> toBeVisited;
  bool initialized = false;
  std::map<int, std::map<int, bool>> visited;
  std::vector<Point2D> getVisitables(World* w, const Point2D& p);
  std::vector<Point2D> getVisitedNeighbors(World* w, const Point2D& p);
  Point2D randomStartPoint(World* world);
  std::vector<Point2D> deltas = {Point2D(0, -1), Point2D(0, 1), Point2D(-1, 0), Point2D(1, 0)};  // N, S, W, E

public:
  PrimExample() = default;
  std::string GetName() override { return "Prim"; };
  bool Step(World* world) override;
  void Clear(World* world) override;
  
};

#endif  // MOBAGEN_EXAMPLES_MAZE_GENERATORS_PRIMEXAMPLE_H_
