#include <iostream>
#include "Search.h"

#include "pieces/Bishop.h"
#include "pieces/King.h"
#include "pieces/Pawn.h"
#include "pieces/Knight.h"
#include "pieces/Queen.h"
#include "pieces/Rook.h"

auto Search::ListMoves(WorldState& state, bool currentPlayer) -> std::vector<Move> {
  vector<Move> moves;
  for (auto line = 0; line < 8; line++) {
    for (auto column = 0; column < 8; column++) {
      Point2D const location = {column, line};
      auto p = state.PieceAtPosition(location);
      // continue if it is not a valid piece
      if (p.Piece() == PieceType::NONE || p.Piece() != PieceType::WRONG || (currentPlayer && p.Color() != state.GetTurn())
          || (!currentPlayer && p.Color() == state.GetTurn()))
        continue;
      vector<Move> toAdd;
      // todo: improve this switch
      switch (p.Piece()) {
        case PieceType::Rook:
          toAdd = Move::GenerateListOfMoves(p, location, Rook::AttackMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Bishop:
          toAdd = Move::GenerateListOfMoves(p, location, Bishop::AttackMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Pawn:
          toAdd = Move::GenerateListOfMoves(p, location, Pawn::PossibleMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Queen:
          toAdd = Move::GenerateListOfMoves(p, location, Queen::AttackMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Knight:
          toAdd = Move::GenerateListOfMoves(p, location, Knight::AttackMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::King:
          toAdd = Move::GenerateListOfMoves(p, location, King::AttackMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
        default:
          break;
      }
    }
  }
  return moves;
}

auto Search::ListPlacesKingCannotGo(WorldState& state, PieceColor turn) -> unordered_set<Point2D> {
  unordered_set<Point2D> moves;
  for (auto line = 0; line < 8; line++) {
    for (auto column = 0; column < 8; column++) {
      Point2D const location = {column, line};
      auto p = state.PieceAtPosition(location);
      // continue if it is not a valid piece
      if (p.Piece() == PieceType::NONE || p.Piece() == PieceType::WRONG) continue;
      if (turn == p.Color()) continue;
      // todo: improve this switch
      switch (p.Piece()) {
        case PieceType::Rook:
          moves.merge(Rook::CoverMoves(state, location));
          break;
        case PieceType::Bishop:
          moves.merge(Bishop::CoverMoves(state, location));
          break;
        case PieceType::Pawn:
          moves.merge(Pawn::CoverMoves(state, location));
          break;
        case PieceType::Queen:
          moves.merge(Queen::CoverMoves(state, location));
          break;
        case PieceType::Knight:
          moves.merge(Knight::CoverMoves(state, location));
          break;
        case PieceType::King:
          moves.merge(King::CoverMovesNaive(state, location));
          break;
        default:
          break;
      }
    }
  }

  cout << "attacked: ";
  for (auto item : moves) cout << item.to_string();
  std::cout << endl;
  return moves;
}
