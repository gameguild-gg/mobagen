#ifndef CHESS_QUEEN_H
#define CHESS_QUEEN_H

#include "../WorldState.h"
#include <unordered_set>
struct Queen {
  static unordered_set<Point2D> AttackMoves(WorldState& world, const Point2D& origin);
  static unordered_set<Point2D> CoverMoves(WorldState& world, const Point2D& origin);
};

#endif  // MOBAGEN_QUEEN_H
