//
// Created by tolstenko on 2/28/23.
//

#include "Pawn.h"
#include <unordered_set>
unordered_set<Point2D> Pawn::PossibleMoves(WorldState& world, const Point2D& pos) {
  auto turn = world.GetTurn();
  auto piece = world.PieceAtPosition(pos);
  if(piece.piece!=PieceType::Pawn)
    return {};
  if(piece.color != turn)
    return {};
  unordered_set<Point2D> points;
  if(piece.color == PieceColor::White){
    auto NorthPiece = world.PieceAtPosition({pos.x,pos.y+1});
    if(NorthPiece.piece == PieceType::NONE)
      points.emplace(pos.x, pos.y+1);
    if(pos.y==1 && NorthPiece.piece == PieceType::NONE) {
      auto NorthNorthPiece = world.PieceAtPosition({pos.x,pos.y + 2});
      if(NorthNorthPiece.piece == PieceType::NONE)
        points.emplace(pos.x, pos.y + 2);
    }
    auto northEastPiece = world.PieceAtPosition({pos.x+1, pos.y+1});
    if(northEastPiece.piece != PieceType::WRONG && northEastPiece.piece != PieceType::NONE && northEastPiece.color == PieceColor::Black)
      points.emplace(pos.x+1, pos.y+1);
    auto northWestPiece = world.PieceAtPosition({pos.x-1, pos.y+1});
    if(northWestPiece.piece != PieceType::WRONG && northWestPiece.piece != PieceType::NONE && northWestPiece.color == PieceColor::Black)
      points.emplace(pos.x-1, pos.y+1);
  }
  else if(piece.color == PieceColor::Black) {
    auto NorthPiece = world.PieceAtPosition({pos.x,pos.y-1});
    if(NorthPiece.piece == PieceType::NONE)
      points.emplace(pos.x, pos.y-1);
    if(pos.y==6 && NorthPiece.piece == PieceType::NONE) {
      auto NorthNorthPiece = world.PieceAtPosition({pos.x,pos.y - 2});
      if(NorthNorthPiece.piece == PieceType::NONE)
        points.emplace(pos.x, pos.y - 2);
    }
    auto northEastPiece = world.PieceAtPosition({pos.x+1, pos.y-1});
    if(northEastPiece.piece != PieceType::WRONG && northEastPiece.piece != PieceType::NONE && northEastPiece.color == PieceColor::White)
      points.emplace(pos.x+1, pos.y-1);
    auto northWestPiece = world.PieceAtPosition({pos.x-1, pos.y-1});
    if(northWestPiece.piece != PieceType::WRONG && northWestPiece.piece != PieceType::NONE && northWestPiece.color == PieceColor::White)
      points.emplace(pos.x-1, pos.y-1);
  }
  return points;
}