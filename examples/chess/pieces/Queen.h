#ifndef CHESS_QUEEN_H
#define CHESS_QUEEN_H

#include "PieceBase.h"
class Queen: public PieceBase {
  vector<Point2D> PossibleMoves(const WorldState& world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(const WorldState& world, const Point2D& origin) override;
};

#endif  // MOBAGEN_QUEEN_H
