#ifndef CHESS_BISHOP_H
#define CHESS_BISHOP_H

#include "../WorldState.h"
struct Bishop: public PieceBase {
  vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) override;
};

#endif
