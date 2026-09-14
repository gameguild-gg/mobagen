#ifndef RECURSIVEBACKTRACKER_H
#define RECURSIVEBACKTRACKER_H

#include "../MazeGeneratorBase.h"
#include <string>
#include "math/Point2D.h"
#include <map>
#include <vector>

class RecursiveBacktrackerExample : public MazeGeneratorBase {
private:
  // the path is tracked in FORMAL units: (0, 0) is the top-left cell,
  // x grows right and y grows down; World::ToWorldCoords/ToFormalCoords
  // translate to and from the world's centered units.
  std::vector<Point2D> stack;
  std::map<int, std::map<int, bool>> visited;  // naive. not optimal
  std::vector<Point2D> getVisitables(World* w, const Point2D& formalPoint);

public:
  RecursiveBacktrackerExample() = default;
  std::string GetName() override { return "Recursive Back-Tracker"; };
  bool Step(World* world) override;
  void Clear(World* world) override;
};

#endif  // RECURSIVEBACKTRACKER_H
