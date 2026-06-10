#ifndef CHESS_PAWN_H
#define CHESS_PAWN_H

#include "../WorldState.h"
#include "Polygon.h"
#include <unordered_set>
struct Pawn {
public:
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static unordered_set<Point2D> AttackMoves(WorldState& world, const Point2D& origin);
  static unordered_set<Point2D> CoverMoves(WorldState& world, const Point2D& origin);
  static int CountDoubles(WorldState& world, const Point2D& origin);
  static bool IsIsolated(WorldState& world, const Point2D& origin);
};

#endif  // MOBAGEN_PAWN_H
