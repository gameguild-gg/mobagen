#ifndef CHESS_BISHOP_H
#define CHESS_BISHOP_H

#include "../WorldState.h"
#include <unordered_set>
struct Bishop {
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) {return {};};
};

#endif
