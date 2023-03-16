#include "Pawn.h"
#include <unordered_set>
// todo: en-passant move. store state to restrict only one en-passant per pawn
unordered_set<Point2D> Pawn::PossibleMoves(WorldState& world, const Point2D& pos) {
  auto piece = world.PieceAtPosition(pos);
  if (piece.piece != PieceType::Pawn) return {};

  unordered_set<Point2D> points;
  if (piece.color == PieceColor::White) {
    auto NorthPiece = world.PieceAtPosition({pos.x, pos.y + 1});
    if (NorthPiece.piece == PieceType::NONE) points.emplace(pos.x, pos.y + 1);
    if (pos.y == 1 && NorthPiece.piece == PieceType::NONE) {
      auto NorthNorthPiece = world.PieceAtPosition({pos.x, pos.y + 2});
      if (NorthNorthPiece.piece == PieceType::NONE) points.emplace(pos.x, pos.y + 2);
    }
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y + 1});
    if (northEastPiece.piece != PieceType::WRONG && northEastPiece.piece != PieceType::NONE && northEastPiece.color == PieceColor::Black)
      points.emplace(pos.x + 1, pos.y + 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y + 1});
    if (northWestPiece.piece != PieceType::WRONG && northWestPiece.piece != PieceType::NONE && northWestPiece.color == PieceColor::Black)
      points.emplace(pos.x - 1, pos.y + 1);
  } else if (piece.color == PieceColor::Black) {
    auto NorthPiece = world.PieceAtPosition({pos.x, pos.y - 1});
    if (NorthPiece.piece == PieceType::NONE) points.emplace(pos.x, pos.y - 1);
    if (pos.y == 6 && NorthPiece.piece == PieceType::NONE) {
      auto NorthNorthPiece = world.PieceAtPosition({pos.x, pos.y - 2});
      if (NorthNorthPiece.piece == PieceType::NONE) points.emplace(pos.x, pos.y - 2);
    }
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y - 1});
    if (northEastPiece.piece != PieceType::WRONG && northEastPiece.piece != PieceType::NONE && northEastPiece.color == PieceColor::White)
      points.emplace(pos.x + 1, pos.y - 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y - 1});
    if (northWestPiece.piece != PieceType::WRONG && northWestPiece.piece != PieceType::NONE && northWestPiece.color == PieceColor::White)
      points.emplace(pos.x - 1, pos.y - 1);
  }
  return points;
}
unordered_set<Point2D> Pawn::AttackMoves(WorldState& world, const Point2D& pos) {
  auto piece = world.PieceAtPosition(pos);
  if (piece.piece != PieceType::Pawn) return {};

  unordered_set<Point2D> points;
  if (piece.color == PieceColor::White) {
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y + 1});
    if (northEastPiece.piece == PieceType::NONE || (northEastPiece.piece != PieceType::WRONG && northEastPiece.color == PieceColor::Black))
      points.emplace(pos.x + 1, pos.y + 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y + 1});
    if (northWestPiece.piece == PieceType::NONE || (northWestPiece.piece != PieceType::WRONG && northWestPiece.color == PieceColor::Black))
      points.emplace(pos.x - 1, pos.y + 1);
  } else if (piece.color == PieceColor::Black) {
    auto northEastPiece = world.PieceAtPosition({pos.x + 1, pos.y - 1});
    if (northEastPiece.piece == PieceType::NONE || (northEastPiece.piece != PieceType::WRONG && northEastPiece.color == PieceColor::White))
      points.emplace(pos.x + 1, pos.y - 1);
    auto northWestPiece = world.PieceAtPosition({pos.x - 1, pos.y - 1});
    if (northWestPiece.piece == PieceType::NONE || (northWestPiece.piece != PieceType::WRONG && northWestPiece.color == PieceColor::White))
      points.emplace(pos.x - 1, pos.y - 1);
  }
  return points;
}
int Pawn::CountDoubles(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.piece != PieceType::Pawn) return 0;
  int doubles=0;

  for(auto y=0; y<8; y++){
    auto other = world.PieceAtPosition(Point2D(origin.x, y));
    if(other.piece==PieceType::Pawn && other.color==piece.color)
      doubles++;
  }
  return doubles-1;
}
bool Pawn::IsIsolated(WorldState& world, const Point2D& origin) {
  auto piece = world.PieceAtPosition(origin);
  if (piece.piece != PieceType::Pawn) return true; // todo: communicate errors better
  const vector<Point2D> adjacency = {
      origin.Right(),
      origin.Left(),
      origin.Up().Left(),
      origin.Up().Right(),
      origin.Down().Left(),
      origin.Down().Right(),
      origin.Up(),
      origin.Down()};

  for(auto pos: adjacency) {
    auto other = world.PieceAtPosition(pos);
    if(other==piece)
      return false;
  }
  return true;
}
