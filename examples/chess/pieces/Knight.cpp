#include "Knight.h"
unordered_set<Point2D> Knight::PossibleMoves(WorldState& world,
                                             const Point2D& origin) {
  auto turn = world.GetTurn();
  auto piece = world.PieceAtPosition(origin);
  if(piece.piece!=PieceType::Knight || piece.color != turn)
    return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const deltas = {{-1,2}, {1,2}, {-2,1}, {-2,1}, {-2,-1}, {-2,-1}, {-1,-2}, {1,-2}};
  for(auto delta : deltas){
    auto other = world.PieceAtPosition(delta + origin);
    if(other.piece == PieceType::WRONG || other.color == piece.color)
      continue;
    moves.insert(delta);
  }
  return moves;
}
