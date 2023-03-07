#include "Bishop.h"
unordered_set<Point2D> Bishop::PossibleMoves(WorldState& world, const Point2D& origin) {
  auto turn = world.GetTurn();
  auto piece = world.PieceAtPosition(origin);
  if(piece.piece!=PieceType::Bishop || piece.color != turn)
    return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{1,1}, {-1,1}, {1,-1}, {-1,-1}};

  for(auto direction : directions) {
    Point2D currentPosition;
    PieceData otherPiece;
    for(currentPosition = origin+direction,
        otherPiece=world.PieceAtPosition(currentPosition);
         otherPiece.piece!=PieceType::WRONG;
         currentPosition+=direction,
        otherPiece=world.PieceAtPosition(currentPosition)){
      // add if it is empty and continue
      if(otherPiece.piece==PieceType::NONE)
        moves.insert(currentPosition);
      // add if it is the opposite color and break
      else if(otherPiece.color!=piece.color){
        moves.insert(currentPosition);
        break;
      }
      else
        break;
      // if it is the same color
    }
  }
  return moves;
}
