#ifndef CHESS_KNIGHT_H
#define CHESS_KNIGHT_H

#include "../WorldState.h"
#include <unordered_set>
struct Knight {
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) { return {}; };
};

#endif  // CHESS_KNIGHT_H
