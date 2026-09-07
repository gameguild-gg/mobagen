#ifndef LIFE_FSM_CONDITION_H
#define LIFE_FSM_CONDITION_H

#include "AgentContext.h"
#include <memory>

// A predicate over the agent context. A transition fires when its condition
// tests true; conditions never write to the world.
class Condition {
public:
  virtual ~Condition() = default;
  virtual bool Test(const AgentContext& context) = 0;
};

#endif  // LIFE_FSM_CONDITION_H
