#include "Bishop.h"
unordered_set<Point2D> Bishop::AttackMoves(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.Piece() != PieceType::Bishop) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

  for (auto direction : directions) {
    Point2D currentPosition;
    PieceData otherPiece;
    for (currentPosition = origin + direction, otherPiece = world.PieceAtPosition(currentPosition); otherPiece.Piece() != PieceType::WRONG;
         currentPosition += direction, otherPiece = world.PieceAtPosition(currentPosition)) {
      // add if it is empty and continue
      if (otherPiece.Piece() == PieceType::NONE) moves.insert(currentPosition);
      // add if it is the opposite color and break
      else if (otherPiece.Color() != piece.Color()) {
        moves.insert(currentPosition);
        break;
      } else
        break;
      // if it is the same color
    }
  }
  return moves;
}
unordered_set<Point2D> Bishop::CoverMoves(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.Piece() != PieceType::Bishop) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

  for (auto direction : directions) {
    Point2D currentPosition;
    PieceData otherPiece;
    for (currentPosition = origin + direction, otherPiece = world.PieceAtPosition(currentPosition); otherPiece.Piece() != PieceType::WRONG;
         currentPosition += direction, otherPiece = world.PieceAtPosition(currentPosition)) {
      // add if it is empty and continue
      if (otherPiece.Piece() == PieceType::NONE) moves.insert(currentPosition);
      // add if it is the opposite color and break
      else if (otherPiece.Color() == piece.Color()) {
        moves.insert(currentPosition);
        break;
      } else
        break;
      // if it is the same color
    }
  }
  return moves;
}
