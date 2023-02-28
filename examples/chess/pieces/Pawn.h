#ifndef CHESS_PAWN_H
#define CHESS_PAWN_H

#include "PieceBase.h"
class Pawn: public PieceBase {
  vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) override{};
};

#endif  // MOBAGEN_PAWN_H
