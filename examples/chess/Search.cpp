#include "Search.h"

#include "pieces/Bishop.h"
#include "pieces/King.h"
#include "pieces/Pawn.h"
#include "pieces/Knight.h"
#include "pieces/Queen.h"
#include "pieces/Rook.h"

auto Search::ListMovesExceptKing(WorldState& state, bool currentPlayer) -> std::vector<Move> {
  vector<Move> moves;
  for(auto line = 0; line < 8; line++){
    for(auto column = 0; column < 8; column++){
      Point2D const location = {column,line};
      auto p = state.PieceAtPosition(location);
      // continue if it is not a valid piece
      if(p.piece==PieceType::NONE || p.piece!=PieceType::WRONG ||
          (currentPlayer && p.color!=state.GetTurn()) ||
          (!currentPlayer && p.color==state.GetTurn()))
        continue;
      vector<Move>toAdd;
      // todo: improve this switch
      switch (p.piece) {
        case PieceType::Rook:
          toAdd = Move::GenerateListOfMoves(p,location,Rook::PossibleMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Bishop:
          toAdd = Move::GenerateListOfMoves(p,location,Bishop::PossibleMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Pawn:
          toAdd = Move::GenerateListOfMoves(p,location,Pawn::PossibleMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Queen:
          toAdd = Move::GenerateListOfMoves(p,location,Queen::PossibleMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        case PieceType::Knight:
          toAdd = Move::GenerateListOfMoves(p,location,Knight::PossibleMoves(state, location));
          moves.insert(moves.end(), toAdd.begin(), toAdd.end());
          break;
        default:
          break;
      }
    }
  }
  return moves;
}
auto Search::ListMoves(WorldState& state, bool currentPlayer) -> std::vector<Move> {
  auto currentMoves = ListMovesExceptKing(state, currentPlayer);
  auto enemyMoves = ListMovesExceptKing(state, !currentPlayer);

  auto currentKing = King::FindKing(state,currentPlayer);
  auto otherKing = King::FindKing(state,!currentPlayer);

  return {};
}

auto Search::ListPlacesKingCannotGo(WorldState& state, bool currentPlayer) -> unordered_set<Point2D> {
  unordered_set<Point2D> moves;
  auto const turn = state.GetTurn();
  for(auto line = 0; line < 8; line++){
    for(auto column = 0; column < 8; column++){
      Point2D const location = {column,line};
      auto p = state.PieceAtPosition(location);
      // continue if it is not a valid piece
      if(p.piece==PieceType::NONE || p.piece==PieceType::WRONG)
        continue;
      if((turn==p.color && currentPlayer) || (turn!=p.color && !currentPlayer))
        continue;
      // todo: improve this switch
      switch (p.piece) {
        case PieceType::Rook:
          moves.merge(Rook::PossibleMoves(state, location));
          break;
        case PieceType::Bishop:
          moves.merge(Bishop::PossibleMoves(state, location));
          break;
        case PieceType::Pawn:
          moves.merge(Pawn::AttackMoves(state, location));
          break;
        case PieceType::Queen:
          moves.merge(Queen::PossibleMoves(state, location));
          break;
        case PieceType::Knight:
          moves.merge(Knight::PossibleMoves(state, location));
          break;
        default:
          break;
      }
    }
  }

  auto otherKing = King::FindKing(state,!currentPlayer);
  unordered_set<Point2D> otherKingVicinity = {
      otherKing + Point2D::UP + Point2D::LEFT, otherKing+Point2D::UP, otherKing+Point2D::UP + Point2D::RIGHT,
      otherKing + Point2D::DOWN + Point2D::LEFT, otherKing+Point2D::DOWN, otherKing+Point2D::DOWN + Point2D::RIGHT,
      otherKing + Point2D::RIGHT, otherKing + Point2D::LEFT
  };
  moves.merge(otherKingVicinity);
  return moves;
}
