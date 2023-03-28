#include "King.h"
#include "../Search.h"
// todo: castling
// todo: deal with the case that the king cannot move to an attacked square
unordered_set<Point2D> King::AttackMoves(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  auto attackedSquares = Search::ListPlacesKingCannotGo(world, piece.Color());

  if (piece.Piece() != PieceType::King) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

  for (auto direction : directions) {
    auto currentPosition = origin + direction;
    auto otherPiece = world.PieceAtPosition(currentPosition);
    if (otherPiece.Piece() == PieceType::WRONG) continue;
    // add if it is empty and continue
    if ((otherPiece.Piece() == PieceType::NONE || otherPiece.Color() != piece.Color()) && !attackedSquares.contains(currentPosition))
      moves.insert(currentPosition);
  }
  return moves;
}

auto King::FindKing(WorldState& state, PieceColor color) -> Point2D {
  for (auto line = 0; line < 8; line++) {
    for (auto column = 0; column < 8; column++) {
      Point2D location = {column, line};
      auto p = state.PieceAtPosition(location);
      if (p.Color() != color) continue;
      if (p.Piece() == PieceType::King) return location;
    }
  }
  return {};
}
unordered_set<Point2D> King::CoverMovesNaive(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  //  auto attackedSquares = Search::ListPlacesKingCannotGo(world, piece.Color());

  if (piece.Piece() != PieceType::King) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

  for (auto direction : directions) {
    auto currentPosition = origin + direction;
    auto otherPiece = world.PieceAtPosition(currentPosition);
    if (otherPiece.Piece() == PieceType::WRONG) continue;
    // add if it is empty and continue
    if (otherPiece.Piece() == PieceType::NONE || otherPiece.Color() == piece.Color()) moves.insert(currentPosition);
  }
  return moves;
}
auto King::IsInCheck(WorldState& state, PieceColor color) -> int {
  auto kingLocation = FindKing(state, color);
  auto attackedSquares = Search::ListMoves(state, (PieceColor)(!(bool)color));
  int count=0;
  for(auto square : attackedSquares) {
    if (square.To() == kingLocation) count++;
  }
  return count;
}
