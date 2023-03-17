#include "Pawn.h"
#include <unordered_set>
// todo: en-passant move. store state to restrict only one en-passant per pawn
unordered_set<Point2D> Pawn::PossibleMoves(WorldState& world, const Point2D& pos) {
  auto piece = world.PieceAtPosition(pos);
  if (piece.Piece() != PieceType::Pawn) return {};

  unordered_set<Point2D> points;
  if (piece.Color() == PieceColor::White) {
    auto NorthPiece = world.PieceAtPosition({pos.x, pos.y + 1});
    if (NorthPiece.Piece() == PieceType::NONE) points.emplace(pos.x, pos.y + 1);
    if (pos.y == 1 && NorthPiece.Piece() == PieceType::NONE) {
      auto NorthNorthPiece = world.PieceAtPosition({pos.x, pos.y + 2});
      if (NorthNorthPiece.Piece() == PieceType::NONE) points.emplace(pos.x, pos.y + 2);
    }
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y + 1});
    if (northEastPiece.Piece() != PieceType::WRONG && northEastPiece.Piece() != PieceType::NONE && northEastPiece.Color() == PieceColor::Black)
      points.emplace(pos.x + 1, pos.y + 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y + 1});
    if (northWestPiece.Piece() != PieceType::WRONG && northWestPiece.Piece() != PieceType::NONE && northWestPiece.Color() == PieceColor::Black)
      points.emplace(pos.x - 1, pos.y + 1);
  } else if (piece.Color() == PieceColor::Black) {
    auto NorthPiece = world.PieceAtPosition({pos.x, pos.y - 1});
    if (NorthPiece.Piece() == PieceType::NONE) points.emplace(pos.x, pos.y - 1);
    if (pos.y == 6 && NorthPiece.Piece() == PieceType::NONE) {
      auto NorthNorthPiece = world.PieceAtPosition({pos.x, pos.y - 2});
      if (NorthNorthPiece.Piece() == PieceType::NONE) points.emplace(pos.x, pos.y - 2);
    }
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y - 1});
    if (northEastPiece.Piece() != PieceType::WRONG && northEastPiece.Piece() != PieceType::NONE && northEastPiece.Color() == PieceColor::White)
      points.emplace(pos.x + 1, pos.y - 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y - 1});
    if (northWestPiece.Piece() != PieceType::WRONG && northWestPiece.Piece() != PieceType::NONE && northWestPiece.Color() == PieceColor::White)
      points.emplace(pos.x - 1, pos.y - 1);
  }
  return points;
}
unordered_set<Point2D> Pawn::AttackMoves(WorldState& world, const Point2D& pos) {
  auto piece = world.PieceAtPosition(pos);
  if (piece.Piece() != PieceType::Pawn) return {};

  unordered_set<Point2D> points;
  if (piece.Color() == PieceColor::White) {
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y + 1});
    if (northEastPiece.Piece() == PieceType::NONE || (northEastPiece.Piece() != PieceType::WRONG && northEastPiece.Color() == PieceColor::Black))
      points.emplace(pos.x + 1, pos.y + 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y + 1});
    if (northWestPiece.Piece() == PieceType::NONE || (northWestPiece.Piece() != PieceType::WRONG && northWestPiece.Color() == PieceColor::Black))
      points.emplace(pos.x - 1, pos.y + 1);
  } else if (piece.Color() == PieceColor::Black) {
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y - 1});
    if (northEastPiece.Piece() == PieceType::NONE || (northEastPiece.Piece() != PieceType::WRONG && northEastPiece.Color() == PieceColor::White))
      points.emplace(pos.x + 1, pos.y - 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y - 1});
    if (northWestPiece.Piece() == PieceType::NONE || (northWestPiece.Piece() != PieceType::WRONG && northWestPiece.Color() == PieceColor::White))
      points.emplace(pos.x - 1, pos.y - 1);
  }
  return points;
}
int Pawn::CountDoubles(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.Piece() != PieceType::Pawn) return 0;
  int doubles = 0;

  for (auto y = 0; y < 8; y++) {
    auto other = world.PieceAtPosition(Point2D(origin.x, y));
    if (other.Piece() == PieceType::Pawn && other.Color() == piece.Color()) doubles++;
  }
  return doubles - 1;
}
bool Pawn::IsIsolated(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.Piece() != PieceType::Pawn) return true;  // todo: communicate errors better
  const vector<Point2D> adjacency = {origin.Right(),       origin.Left(),         origin.Up().Left(), origin.Up().Right(),
                                     origin.Down().Left(), origin.Down().Right(), origin.Up(),        origin.Down()};

  for (auto pos : adjacency) {
    auto other = world.PieceAtPosition(pos);
    if (other == piece) return false;
  }
  return true;
}
