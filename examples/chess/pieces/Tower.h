#ifndef CHESS_TOWER_H
#define CHESS_TOWER_H

#include "PieceBase.h"
class Tower: public PieceBase {
  vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) override;
};

#endif  // CHESS_TOWER_H
