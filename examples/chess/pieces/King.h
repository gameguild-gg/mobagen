#ifndef CHESS_KING_H
#define CHESS_KING_H
#include "PieceBase.h"

struct King: public PieceBase {
  vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) override{};
  vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) override{};
};

#endif  // MOBAGEN_KING_H
