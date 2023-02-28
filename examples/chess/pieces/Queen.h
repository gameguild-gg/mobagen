#ifndef CHESS_QUEEN_H
#define CHESS_QUEEN_H

#include "../WorldState.h"
struct Queen: public PieceBase {
  vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) override;
  vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) override;
};

#endif  // MOBAGEN_QUEEN_H
