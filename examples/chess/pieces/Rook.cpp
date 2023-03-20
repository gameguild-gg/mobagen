
#include "Rook.h"
// todo: castling behavior
unordered_set<Point2D> Rook::AttackMoves(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.Piece() != PieceType::Rook) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};

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
unordered_set<Point2D> Rook::CoverMoves(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.Piece() != PieceType::Rook) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};

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
