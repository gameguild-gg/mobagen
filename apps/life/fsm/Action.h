#ifndef LIFE_FSM_ACTION_H
#define LIFE_FSM_ACTION_H

#include "AgentContext.h"
#include <memory>

// Something the agent does: entering a state, crossing a transition, or
// staying put. Actions are the only place the simulation is written to.
class Action {
public:
  virtual ~Action() = default;
  virtual void Execute(const AgentContext& context) = 0;
};

#endif  // LIFE_FSM_ACTION_H
