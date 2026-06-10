#ifndef MOBAGEN_SEARCH_H
#define MOBAGEN_SEARCH_H

#include "WorldState.h"

struct Search {
  // todo: create your algorithm to calculate next move
  static Move NextMove(WorldState& state);
  /**
   * @param state world state
   * @param currentPlayer calculate the current turn possible moves
   * @return
   */
  static auto ListPlacesKingCannotGo(WorldState& state, PieceColor turn) -> unordered_set<Point2D>;
  static auto ListMoves(WorldState& state, PieceColor turn) -> std::vector<Move>;
};

#endif  // MOBAGEN_SEARCH_H
