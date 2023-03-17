#include "King.h"
#include "../Search.h"
// todo: castling
// todo: deal with the case that the king cannot move to an attacked square
unordered_set<Point2D> King::PossibleMoves(WorldState& world, const Point2D& origin) {
  auto attackedSquares = Search::ListPlacesKingCannotGo(world, true);
  auto piece = world.PieceAtPosition(origin);

  if (piece.Piece() != PieceType::King) return {};

  unordered_set<Point2D> moves;

  vector<Point2D> const directions = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

  for (auto direction : directions) {
    auto currentPosition = origin + direction;
    auto otherPiece = world.PieceAtPosition(currentPosition);
    // add if it is empty and continue
    if ((otherPiece.Piece() == PieceType::NONE || otherPiece.Color() != piece.Color()) && !attackedSquares.contains(currentPosition))
      moves.insert(currentPosition);
  }
  return moves;
}

auto King::FindKing(WorldState& state, bool currentPlayer) -> Point2D {
  for (auto line = 0; line < 8; line++) {
    for (auto column = 0; column < 8; column++) {
      Point2D location = {column, line};
      auto p = state.PieceAtPosition(location);
      if ((currentPlayer && p.Color() != state.GetTurn()) || (!currentPlayer && p.Color() == state.GetTurn())) continue;
      if (p.Piece() == PieceType::King) return location;
    }
  }
  return {};
}