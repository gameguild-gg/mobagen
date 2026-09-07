#ifndef LIFE_FSM_AGENTCONTEXT_H
#define LIFE_FSM_AGENTCONTEXT_H

#include "../World.h"

// What one agent (a cell) looks like to the machine during a single update.
// The world is the state store: conditions read the current buffer through it,
// actions write the next buffer. The context itself is a read-only snapshot
// built by the rule before the machine runs.
struct AgentContext {
  World& world;        // grid being simulated
  Point2D position;    // which cell this agent is
  bool isAlive;        // state in the current buffer
  int aliveNeighbors;  // neighborhood snapshot for this generation
};

#endif  // LIFE_FSM_AGENTCONTEXT_H
