#ifndef LIFE_FSM_STATE_H
#define LIFE_FSM_STATE_H

#include "Action.h"
#include "Condition.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class State;

// A guarded edge of the graph: fires when the condition tests true, moving the
// agent to the target state. Transition actions run between the exit actions of
// the source state and the entry actions of the target state.
struct Transition {
  std::shared_ptr<Condition> condition;
  std::shared_ptr<State> target;
  std::vector<std::shared_ptr<Action>> actions;
};

// A node of the graph: what the agent does in this situation, and where it can
// go next. A State is BEHAVIOR, not storage: it is shared by every cell and
// holds no per-agent data - which cell is in which state lives in the world
// grid (one bit per cell), and the per-cell view arrives in the AgentContext.
class State {
public:
  explicit State(std::string name) : name(std::move(name)) {}

  const std::string& GetName() const { return name; }

  void AddTransition(std::shared_ptr<Condition> condition, std::shared_ptr<State> target, std::vector<std::shared_ptr<Action>> actions = {}) {
    transitions.push_back(Transition{std::move(condition), std::move(target), std::move(actions)});
  }

  void AddEntryAction(std::shared_ptr<Action> action) { entryActions.push_back(std::move(action)); }
  // Stay action: runs when no transition fires during an update.
  void AddAction(std::shared_ptr<Action> action) { stayActions.push_back(std::move(action)); }
  void AddExitAction(std::shared_ptr<Action> action) { exitActions.push_back(std::move(action)); }

  const std::vector<Transition>& GetTransitions() const { return transitions; }
  const std::vector<std::shared_ptr<Action>>& GetEntryActions() const { return entryActions; }
  const std::vector<std::shared_ptr<Action>>& GetStayActions() const { return stayActions; }
  const std::vector<std::shared_ptr<Action>>& GetExitActions() const { return exitActions; }

private:
  std::string name;
  std::vector<Transition> transitions;
  std::vector<std::shared_ptr<Action>> entryActions;
  std::vector<std::shared_ptr<Action>> stayActions;
  std::vector<std::shared_ptr<Action>> exitActions;
};

#endif  // LIFE_FSM_STATE_H
