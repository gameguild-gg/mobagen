#ifndef CHESS_TOWER_H
#define CHESS_TOWER_H

#include "../WorldState.h"
#include <unordered_set>
#include <Polygon.h>
struct Rook  {
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin) {return {};};
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) {return {};};
};

#endif  // CHESS_TOWER_H
