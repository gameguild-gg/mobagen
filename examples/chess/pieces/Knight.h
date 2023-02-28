#ifndef CHESS_KNIGHT_H
#define CHESS_KNIGHT_H

#include "PieceBase.h"
class Knight: public PieceBase {
  vector<Point2D> PossibleMoves(const WorldState& world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(const WorldState& world, const Point2D& origin) override;
};

#endif  // CHESS_KNIGHT_H
