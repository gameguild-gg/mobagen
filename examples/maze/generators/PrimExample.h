#ifndef MOBAGEN_EXAMPLES_MAZE_GENERATORS_PRIMEXAMPLE_H_
#define MOBAGEN_EXAMPLES_MAZE_GENERATORS_PRIMEXAMPLE_H_

#include <vector>
#include "../MazeGeneratorBase.h"
#include "Point2D.h"
#include <map>

class PrimExample: public MazeGeneratorBase  {
 private:
  std::vector<Point2D> toBeVisited;
  bool initialized = false;
  std::vector<Point2D> getVisitables(World* w, const Point2D& p);
  std::vector<Point2D> getVisitedNeighbors(World* w, const Point2D& p);
 public:
  PrimExample() = default;
  std::string GetName() override {return "Prim";};
  bool Step(World * world) override;
  void Clear(World * world) override;
};

#endif //MOBAGEN_EXAMPLES_MAZE_GENERATORS_PRIMEXAMPLE_H_
