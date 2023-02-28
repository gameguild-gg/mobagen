#ifndef CHESS_KNIGHT_H
#define CHESS_KNIGHT_H

#include "../WorldState.h"
struct Knight: public PieceBase {
  vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) override;
};

#endif  // CHESS_KNIGHT_H
