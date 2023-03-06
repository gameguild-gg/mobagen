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
    auto sum = delta + origin;
    auto other = world.PieceAtPosition(sum);
    if(other.piece == PieceType::WRONG || // outside board
        ((uint8_t)other.piece < (uint8_t)PieceType::WRONG && other.piece!=PieceType::NONE && other.color == piece.color)) // same side piece
      continue;
    moves.insert(sum);
  }
  return moves;
}
