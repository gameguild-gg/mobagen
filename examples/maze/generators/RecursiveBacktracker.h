#ifndef RECURSIVEBACKTRACKER_H
#define RECURSIVEBACKTRACKER_H

#include "../MazeGeneratorBase.h"
#include "Point2D.h"
#include <map>
#include <vector>

class RecursiveBacktracker: public MazeGeneratorBase {
 private:
  Point2D start;
  Point2D current;
  std::vector<Point2D> stack;
  std::map<int, std::map<int, bool>> visited; // naive. not optimal
 public:
  void Step(World * world) override;
  void Clear(World * world) override;
};

#endif  // RECURSIVEBACKTRACKER_H
