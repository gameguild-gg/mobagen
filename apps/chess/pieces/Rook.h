#ifndef CHESS_TOWER_H
#define CHESS_TOWER_H

#include "../WorldState.h"
#include <unordered_set>
#include <Polygon.h>
struct Rook {
  static unordered_set<Point2D> AttackMoves(WorldState& world, const Point2D& origin);
  static unordered_set<Point2D> CoverMoves(WorldState& world, const Point2D& origin);
};

#endif  // CHESS_TOWER_H
