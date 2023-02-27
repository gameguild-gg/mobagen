#include "WorldState.h"
PieceState* WorldState::PieceAtPosition(Point2D pos) {
    for(auto & i : board) {
      if(i.position == pos) {
        return &i;
      }
    }
    return {};
  }
