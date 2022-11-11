#ifndef RECURSIVEBACKTRACKER_H
#define RECURSIVEBACKTRACKER_H

#include "../MazeGeneratorBase.h"
#include "Point2D.h"
#include <map>
#include <vector>

class RecursiveBacktrackerExample : public MazeGeneratorBase {
 private:
  std::vector<Point2D> stack;
  std::map<int, std::map<int, bool>> visited; // naive. not optimal
  Point2D randomStartPoint(World* world);
  std::vector<Point2D> getVisitables(World* w, const Point2D& p);
 public:
  RecursiveBacktrackerExample() = default;
  std::string GetName() override {return "Recursive Back-Tracker";};
  bool Step(World * world) override;
  void Clear(World * world) override;
};

#endif  // RECURSIVEBACKTRACKER_H
