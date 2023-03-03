#ifndef CHESS_TOWER_H
#define CHESS_TOWER_H

#include "../WorldState.h"
#include <unordered_set>
#include <Polygon.h>
struct Rook  {
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin) {return {};};
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) {return {};};
  inline static Polygon polygon = Polygon({
      {-0.6,0.73}, {0.6,0.73}, {0.6,0.6}, {-0.6,0.6}, {-0.6,0.73},
      {-0.47,0.6}, {-0.47,0.42}, {0.47,0.42}, {0.47,0.6}, {-0.47,0.6},
      {-0.51,-0.37}, {-0.51,-0.6}, {-0.33,-0.6}, {-0.33,-0.51}, {-0.11,-0.51}, {-0.11,-0.6}, {0.11,-0.6}, {0.11,-0.51}, {0.33,-0.51}, {0.33,-0.6}, {0.51,-0.6}, {0.51,-0.37},
      {0.51,-0.37}, {0.38,0.24}, {-0.37,0.24}, {-0.51,-0.37},
      {0.38,0.24}, {0.38,0.31}, {-0.37,0.31}, {-0.37,0.24},
      {0.38,0.31}, {0.44,0.42}, {-0.44,0.42}, {-0.37,0.31},
      {-0.51,-0.37}, {0.51,-0.37}
  });
};

#endif  // CHESS_TOWER_H
