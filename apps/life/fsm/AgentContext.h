#ifndef LIFE_FSM_AGENTCONTEXT_H
#define LIFE_FSM_AGENTCONTEXT_H

#include "../World.h"

// What one agent (a cell) looks like to the machine during a single update.
//
// Data ownership and lifetime:
//   - the cell's persistent state is ONE BIT in the world grid, that bit is
//     what the double buffering swaps, generation after generation;
//   - State objects are shared behavior nodes and store nothing per cell;
//   - this context is a throwaway snapshot for one update: where the cell is
//     (position), what it is (isAlive, synced from the world bit) and what it
//     sees (aliveNeighbors, from the current buffer only).
// Conditions read the context; actions write the next buffer through
// context.world.SetNext. Nothing here survives the update.
struct AgentContext {
  World& world;        // grid being simulated
  Point2D position;    // which cell this agent is
  bool isAlive;        // state in the current buffer
  int aliveNeighbors;  // neighborhood snapshot for this generation
};

#endif  // LIFE_FSM_AGENTCONTEXT_H
