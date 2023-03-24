#include "Heuristics.h"
#include "WorldState.h"
#include "pieces/Pieces.h"

Heuristics Heuristics::BoardAnalysis(WorldState* state) {
  // todo: write your own heuristic here
  return {};
}

int Heuristics::MaterialScore(WorldState* state) {
  int score = 0;
  for (auto line = 0; line < 8; line++) {
    for (auto column = 0; column < 8; column++) {
      auto location = Point2D(column, line);
      auto piece = state->PieceAtPosition(location);
      auto pieceScore = 0;
      int moves;
      switch (piece.Piece()) {
        case PieceType::King:
          pieceScore += 1000;                                        // piece value
          pieceScore += King::AttackMoves(*state, location).size();  // mobility
          pieceScore += distanceToCenter(location);
          // todo: king safety, check, draw and mate
          break;
        case PieceType::Queen:
          pieceScore += 90;                                           // piece value
          pieceScore += Queen::AttackMoves(*state, location).size();  // mobility
          pieceScore += distanceToCenter(location);
          break;
        case PieceType::Rook:
          pieceScore += 50;                                          // piece value
          pieceScore += Rook::AttackMoves(*state, location).size();  // mobility
          pieceScore += distanceToCenter(location);
          break;
        case PieceType::Knight:
          pieceScore += 35;                                            // piece value
          pieceScore += Knight::AttackMoves(*state, location).size();  // mobility
          pieceScore += distanceToCenter(location);
          break;
        case PieceType::Bishop:
          pieceScore += 30;                                            // piece value
          pieceScore += Bishop::AttackMoves(*state, location).size();  // mobility
          pieceScore += distanceToCenter(location);
          break;
        case PieceType::Pawn:
          pieceScore += 10;                                      // piece value
          moves = Pawn::PossibleMoves(*state, location).size();  // mobility
          pieceScore += distanceToCenter(location);
          pieceScore += moves;
          if (moves == 0) pieceScore -= 5;                          // blocked
          pieceScore -= 2 * Pawn::CountDoubles(*state, location);   // doubled
          if (Pawn::IsIsolated(*state, location)) pieceScore -= 5;  // isolation
          break;
        default:
          continue;
      }
      if (piece.Color() == PieceColor::Black)
        score -= pieceScore;
      else
        score += pieceScore;
    }
  }
  return score;
}
int Heuristics::distanceToCenter(Point2D location) {
  // todo: improve. I am unsure if this is the best way. I am doubling because I am targeting the center.
  auto doubled = Point2D(location.x * 2 - 7, location.y * 2 - 7);
  auto maxed = 3 - (std::min(std::abs(doubled.x), std::abs(doubled.y)) - 1) / 2;
  return maxed;
}
