//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
#include "../fsm/Action.h"
#include "../fsm/AgentContext.h"
#include "../fsm/Condition.h"

#include <stdexcept>

// Hexagonal variant: each cell has 6 neighbors instead of 8. This one is
// interactive-only (no formal fixtures), so the exact rule is up to you - the
// classic hex grid plays B2/S34: a dead cell is born with exactly 2 live
// neighbors, a live cell survives with 3 or 4.
//
// hint: the app draws odd rows displaced by half a cell, so the neighbors
// above and below shift by one column depending on the row parity.
// Reference: https://arunarjunakani.github.io/HexagonalGameOfLife/
//
// The rules as machine parts (same shape as JohnConway):
//   underpopulation (<3) / overpopulation (>4) -> conditions that leave Alive
//   reproduction (==2)                          -> condition that leaves Dead
//   survival is implicit: no transition firing means the stay actions run.

// begin solution
class Underpopulation : public Condition {
public:
  bool Test(const AgentContext& context) override {
    // todo: implement the underpopulation condition
    // hint: on the hex grid (B2/S34) a live cell is underpopulated below 3 neighbors
    throw std::logic_error("Underpopulation condition not implemented yet");
  }
};

class Overpopulation : public Condition {
public:
  bool Test(const AgentContext& context) override {
    // todo: implement the overpopulation condition
    // hint: on the hex grid (B2/S34) a live cell is overpopulated above 4 neighbors
    throw std::logic_error("Overpopulation condition not implemented yet");
  }
};

class Reproduction : public Condition {
public:
  bool Test(const AgentContext& context) override {
    // todo: implement the reproduction condition
    // hint: on the hex grid (B2/S34) a dead cell is born with exactly 2 neighbors
    throw std::logic_error("Reproduction condition not implemented yet");
  }
};

class DieAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    // todo: implement the die action
    // hint:
    //   use the context.world.SetNext() to set the next state of the cell to dead
    //   use the context.position to get the current cell's position
    throw std::logic_error("Die action not implemented yet");
  }
};

class BornAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    // see hints in DieAction
    throw std::logic_error("Born action not implemented yet");
  }
};

class StayAliveAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    // see hints in DieAction
    throw std::logic_error("StayAlive action not implemented yet");
  }
};

class StayDeadAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    // see hints in DieAction
    throw std::logic_error("StayDead action not implemented yet");
  }
};

// end solution

HexagonGameOfLife::HexagonGameOfLife() {
  alive = std::make_shared<State>("Alive");
  dead = std::make_shared<State>("Dead");

  const auto die = std::make_shared<DieAction>();
  const auto born = std::make_shared<BornAction>();

  // todo: add transitions and actions for alive, dead. example:
  //   alive->AddTransition(std::make_shared<Underpopulation>(), dead, {die});
  //   dead->AddAction(std::make_shared<StayDeadAction>());
  // begin solution

  // end solution
}

void HexagonGameOfLife::Step(World& world) {
  // relevant functions:
  //   world.Height() and world.Width() to get the world dimensions,
  //   world.Get() reads the CURRENT generation, world.SetNext() writes the NEXT one
  // Build one context per cell and let the machine decide: conditions read the
  // current generation through the context, actions write the next one.
  //
  // note: the double buffering does NOT happen here. Your actions only write
  // the next buffer via SetNext; the demo app's Manager::step calls
  // world.SwapBuffers() right AFTER this function returns. Never call
  // SwapBuffers from inside a rule.
  // begin solution
  for (int y = 0; y < world.Height(); ++y) {
    for (int x = 0; x < world.Width(); ++x) {
      AgentContext context{world, {x, y}, world.Get({x, y}), CountNeighbors(world, {x, y})};
      machine.SetCurrent(context.isAlive ? alive : dead);
      machine.Update(context);
    }
  }
  // end solution
}

int HexagonGameOfLife::CountNeighbors(World& world, Point2D point) {
  // todo: count the ALIVE neighbors of the cell at point, on the hex grid
  // hint:
  //   a hex cell has 6 neighbors: left and right on the same row, plus two
  //   above and two below, shifted by one column depending on the row parity
  //   world.Get() wraps around the borders (toroidal)
  // begin solution
  throw std::logic_error("CountNeighbors not implemented yet");
  // end solution
}
