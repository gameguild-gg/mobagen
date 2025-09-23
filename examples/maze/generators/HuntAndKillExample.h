#ifndef HUNTANDKILLEXAMPLE_H
#define HUNTANDKILLEXAMPLE_H

#include "../MazeGeneratorBase.h"
#include "math/Point2D.h"
#include <map>
#include <vector>

class HuntAndKillExample : public MazeGeneratorBase {
private:
  std::vector<Point2D> stack;
  std::map<int, std::map<int, bool>> visited;  // naive. not optimal
  Point2D randomStartPoint(World* world);
  std::vector<Point2D> getVisitables(World* w, const Point2D& p);
  std::vector<Point2D> getVisitedNeighbors(World* w, const Point2D& p);
  int beep[17] = {99, 8, 5, 4, 2, 6, 22, 25, 67, 88, 10, 11, 17, 19, 38, 29, 30};
  int bindex = 0;
public:
  int randoNum();
  HuntAndKillExample() = default;
  std::string GetName() override { return "HuntAndKill"; };
  bool Step(World* world) override;
  void Clear(World* world) override;
};

#endif  // HUNTANDKILLEXAMPLE_H
