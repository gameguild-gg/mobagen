#include <iostream>
#include "Search.h"
#include "Heuristics.h"
#include "pieces/Pieces.h"
#include "algorithm"

auto Search::ListMoves(WorldState& state, PieceColor turn) -> std::vector<Move> {
  vector<Move> moves;
  for (auto line = 0; line < 8; line++) {
    for (auto column = 0; column < 8; column++) {
      Point2D const location = {column, line};
      auto p = state.PieceAtPosition(location);
      // continue if it is not a valid piece
      if (p.Piece() == PieceType::NONE || turn != p.Color()) continue;
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

  return moves;
}

Move Search::NextMove(WorldState& state) {
  // list all possible moves
  // for every node, go deep and start the recursion up to a specific depth
  // if we reach the leaves, stop deepening and calculate the score
  // after all the recursion, you have to sort all the children from that node
  // selectively choose the best ones (max or min) based on the current deepening level, and mark your score as the result of this search


  auto moves = Search::ListMoves(state, state.GetTurn());
  // generate states
  vector<MoveState> moveStates;
  for (auto move : moves) {
    auto newState = state;
    newState.Move(move.From(), move.To());
    auto score = Heuristics::MaterialScore(&newState);
    moveStates.push_back({newState, {move}, score});
  }
  // do not cover the worst ones, cut the queue in half
  if (moveStates[0].state.GetTurn() == PieceColor::White)
    sort(moveStates.begin(), moveStates.end());
  else
    sort(moveStates.rbegin(), moveStates.rend());
  // moveStates.resize(min(moveStates.size() / 2, (unsigned long int)10));

  // todo: loop this or make it be recursive
  vector<MoveState> moveStates2;
  // go deeper for the opponent color
  for (auto state : moveStates) {
    auto moves = Search::ListMoves(state.state, state.state.GetTurn());
    for (auto move : moves) {
      auto newState = state;
      newState.moves.push_back(move);
      newState.state.Move(move.From(), move.To());
      newState.score = Heuristics::MaterialScore(&newState.state);
      moveStates2.push_back(newState);
    }
  }
  // do not cover the worst ones, cut the queue in half
  if (moveStates2[0].state.GetTurn() == PieceColor::Black)
    sort(moveStates2.begin(), moveStates2.end());
  else
    sort(moveStates2.rbegin(), moveStates2.rend());
  // moveStates2.resize(min(moveStates2.size() / 2, (unsigned long int)10));

  vector<MoveState> moveStates3;
  // go deeper for the opponent color
  for (auto state : moveStates2) {
    auto moves = Search::ListMoves(state.state, state.state.GetTurn());
    for (auto move : moves) {
      auto newState = state;
      newState.moves.push_back(move);
      newState.state.Move(move.From(), move.To());
      newState.score = Heuristics::MaterialScore(&newState.state);
      moveStates3.push_back(newState);
    }
  }
  // do not cover the worst ones, cut the queue in half
  if (moveStates3[0].state.GetTurn() == PieceColor::White)
    sort(moveStates3.begin(), moveStates3.end());
  else
    sort(moveStates3.rbegin(), moveStates3.rend());
  // moveStates3.resize(min(moveStates2.size() / 2, (unsigned long int)10));

  return moveStates3[0].moves[0];
}
