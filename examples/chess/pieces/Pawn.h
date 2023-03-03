#ifndef CHESS_PAWN_H
#define CHESS_PAWN_H

#include "../WorldState.h"
#include "Polygon.h"
#include <unordered_set>
struct Pawn {
 public:
  static unordered_set<Point2D> PossibleMoves(WorldState& world, const Point2D& origin);
  static vector<WorldState> GenerateStates(WorldState& world, const Point2D& origin) {return {};};
  inline static Polygon polygon = Polygon(
      {{0,0.6}, {-0.18,0.43}, {-0.16,0.31},{-0.29,0.09},{-0.18,-0.15},{-0.5,-0.75},
          {0.5,-0.75}, {0.18,-0.15}, {0.29,0.09}, {0.16,0.31}, {0.18,0.43}}
 );
};

#endif  // MOBAGEN_PAWN_H
