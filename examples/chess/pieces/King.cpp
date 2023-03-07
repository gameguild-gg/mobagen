#include "King.h"
// todo: castling
// todo: deal with the case that the king cannot move to an attacked square
unordered_set<Point2D> King::PossibleMoves(WorldState& world,
                                           const Point2D& origin) {
  auto turn = world.GetTurn();
  auto piece = world.PieceAtPosition(origin);
  if(piece.piece!=PieceType::King || piece.color != turn)
    return {};

  unordered_set<Point2D> moves;

  vector<Point2D> directions = {{0,1}, {0,-1}, {1,0}, {-1,0}, {1,1}, {-1,1}, {1,-1}, {-1,-1}};

  for(auto direction : directions) {
    auto currentPosition = origin+direction;
    auto otherPiece = world.PieceAtPosition(currentPosition);
    // add if it is empty and continue
    if(otherPiece.piece==PieceType::NONE || otherPiece.color!=piece.color)
      moves.insert(currentPosition);
  }
  return moves;
}
