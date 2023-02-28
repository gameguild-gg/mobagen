#ifndef CHESS_BISHOP_H
#define CHESS_BISHOP_H

#include "PieceBase.h"
class Bishop: public PieceBase {
  vector<Point2D> PossibleMoves(const WorldState& world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(const WorldState& world, const Point2D& origin) override;
};

#endif
