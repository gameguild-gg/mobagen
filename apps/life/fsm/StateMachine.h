#ifndef LIFE_FSM_STATEMACHINE_H
#define LIFE_FSM_STATEMACHINE_H

#include "AgentContext.h"
#include "State.h"

#include <memory>

// The classical finite state machine (Millington, "AI for Games", ch. 5).
// Division of labor: the world bit stores WHERE each cell is; this machine
// decides WHAT happens next. 'current' below is not storage - it is a cursor
// over the shared state graph, synced from the world bit (via SetCurrent)
// before each Update, moved by the transition that fires.
class StateMachine {
public:
  StateMachine() = default;

  // Points the cursor at the node matching the cell's persistent bit
  // (world.Get). No actions run here - it is a plain sync, done by the rule
  // right before Update.
  void SetCurrent(std::shared_ptr<State> state) { current = std::move(state); }

  const std::shared_ptr<State>& GetCurrent() const { return current; }

  // Runs one update for the agent described by the context:
  // 1. scan the current state's transitions in registration order;
  // 2. on the first condition that tests true: exit actions of the current
  //    state, transition actions, entry actions of the target state;
  // 3. if none fires: the current state's stay actions run.
  // Returns true when a transition fired.
  bool Update(const AgentContext& context);

private:
  std::shared_ptr<State> current;
};

inline bool StateMachine::Update(const AgentContext& context) {
  for (const Transition& transition : current->GetTransitions()) {
    if (transition.condition->Test(context)) {
      for (const auto& action : current->GetExitActions()) action->Execute(context);
      for (const auto& action : transition.actions) action->Execute(context);
      for (const auto& action : transition.target->GetEntryActions()) action->Execute(context);
      current = transition.target;
      return true;
    }
  }
  for (const auto& action : current->GetStayActions()) action->Execute(context);
  return false;
}

#endif  // LIFE_FSM_STATEMACHINE_H
