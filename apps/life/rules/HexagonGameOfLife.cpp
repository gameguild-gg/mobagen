//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
#include "../fsm/Action.h"
#include "../fsm/AgentContext.h"
#include "../fsm/Condition.h"
#include "../fsm/State.h"
#include "../fsm/StateMachine.h"

// Hexagonal variant: each cell has 6 neighbors instead of 8. This one is
// interactive-only (no formal fixtures), so the exact rule is up to you - the
// classic hex grid plays B2/S34: a dead cell is born with exactly 2 live
// neighbors, a live cell survives with 3 or 4.
//
// hint: the app draws odd rows displaced by half a cell, so the neighbors
// above and below shift by one column depending on the row parity.

void HexagonGameOfLife::Step(World& world) {
  // same shape as JohnConway::Step: build an AgentContext per cell and run
  // your machine - conditions read the snapshot, actions write SetNext
  // begin solution

  // end solution
}

int HexagonGameOfLife::CountNeighbors(World& world, Point2D point) {
  // hint: 6 neighbors - left and right on the same row, plus two above and
  // two below, shifted by the row parity (World::Get wraps them toroidally)
  // begin solution

  // end solution
  return 0;
}
