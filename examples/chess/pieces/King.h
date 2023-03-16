#ifndef CHESS_KING_H
#define CHESS_KING_H
#include "../WorldState.h"
#include <unordered_set>

struct King {
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) { return {}; };
  static auto FindKing(WorldState& state, bool currentPlayer) -> Point2D;
};

#endif  // MOBAGEN_KING_H
