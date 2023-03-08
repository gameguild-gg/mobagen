#ifndef CHESS_PAWN_H
#define CHESS_PAWN_H

#include "../WorldState.h"
#include "Polygon.h"
#include <unordered_set>
struct Pawn {
public:
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) { return {}; };
};

#endif  // MOBAGEN_PAWN_H
