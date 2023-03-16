#ifndef MOBAGEN_SEARCH_H
#define MOBAGEN_SEARCH_H

#include "WorldState.h"

struct Search {
  // todo: create your algorithm to calculate next move
  static Move NextMove(const WorldState& state) { return {}; };
  /**
   * @param state world state
   * @param currentPlayer calculate the current turn possible moves
   * @return
   */
  static auto ListPlacesKingCannotGo(WorldState& state, bool currentPlayer) -> unordered_set<Point2D>;
  static auto ListMovesExceptKing(WorldState& state, bool currentPlayer) -> std::vector<Move>;
  static auto ListMoves(WorldState& state, bool currentPlayer) -> std::vector<Move>;
  static auto FindKing(WorldState& state, bool currentPlayer) -> Point2D;
  static std::vector<Move> ListMoves(const WorldState& state);
};

#endif  // MOBAGEN_SEARCH_H
