#ifndef LIFE_FSM_STATEMACHINE_H
#define LIFE_FSM_STATEMACHINE_H

#include "AgentContext.h"
#include "State.h"

#include <memory>

// The classical finite state machine (Millington, "AI for Games", ch. 5):
// holds the current state of one agent and fires the first transition whose
// condition tests true.
class StateMachine {
public:
  StateMachine() = default;

  // Syncs the machine with where the agent currently is. No actions run here:
  // the world is the state store, so the rule sets this from the current
  // buffer before each update.
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
  // begin solution

  // end solution
  return false;
}

#endif  // LIFE_FSM_STATEMACHINE_H
