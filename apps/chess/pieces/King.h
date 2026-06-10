#ifndef CHESS_KING_H
#define CHESS_KING_H
#include "../WorldState.h"
#include <unordered_set>

struct King {
  static unordered_set<Point2D> AttackMoves(WorldState& world, const Point2D& origin);
  static unordered_set<Point2D> CoverMovesNaive(WorldState& world, const Point2D& origin);
  static auto FindKing(WorldState& state, PieceColor color) -> Point2D;
  static auto IsInCheck(WorldState& state, PieceColor color) -> int;
};

#endif  // MOBAGEN_KING_H
