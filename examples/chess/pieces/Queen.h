#ifndef CHESS_QUEEN_H
#define CHESS_QUEEN_H

#include "../WorldState.h"
#include <unordered_set>
struct Queen {
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) { return {}; };
};

#endif  // MOBAGEN_QUEEN_H
