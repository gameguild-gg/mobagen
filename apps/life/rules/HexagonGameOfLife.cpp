//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
#include "../fsm/Action.h"
#include "../fsm/AgentContext.h"
#include "../fsm/Condition.h"

#include <SDL3/SDL_log.h>

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
namespace hexagon {
class Underpopulation : public Condition {
public:
  bool Test(const AgentContext& context) override {
    // on the hex grid (B2/S34) a live cell is underpopulated below 3 neighbors
    return context.isAlive && context.aliveNeighbors < 3;
  }
};

class Overpopulation : public Condition {
public:
  bool Test(const AgentContext& context) override {
    // on the hex grid (B2/S34) a live cell is overpopulated above 4 neighbors
    return context.isAlive && context.aliveNeighbors > 4;
  }
};

class Reproduction : public Condition {
public:
  bool Test(const AgentContext& context) override {
    // on the hex grid (B2/S34) a dead cell is born with exactly 2 neighbors
    return !context.isAlive && context.aliveNeighbors == 2;
  }
};

class DieAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    context.world.SetNext(context.position, false);
  }
};

class BornAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    context.world.SetNext(context.position, true);
  }
};

class StayAliveAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    context.world.SetNext(context.position, true);
  }
};

class StayDeadAction : public Action {
public:
  void Execute(const AgentContext& context) override {
    context.world.SetNext(context.position, false);
  }
};
}  // namespace hexagon

// end solution

HexagonGameOfLife::HexagonGameOfLife() {
  using namespace hexagon;

  alive = std::make_shared<State>("Alive");
  dead = std::make_shared<State>("Dead");

  const auto die = std::make_shared<DieAction>();
  const auto born = std::make_shared<BornAction>();
  // begin solution

  alive->AddTransition(std::make_shared<Underpopulation>(), dead, {die});
  alive->AddTransition(std::make_shared<Overpopulation>(), dead, {die});
  alive->AddAction(std::make_shared<StayAliveAction>());

  dead->AddTransition(std::make_shared<Reproduction>(), alive, {born});
  dead->AddAction(std::make_shared<StayDeadAction>());

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
  // hint:
  //   a hex cell has 6 neighbors: left and right on the same row, plus two
  //   above and two below, shifted by one column depending on the row parity
  //   world.Get() wraps around the borders (toroidal)
  // begin solution

  // Odd-row offset layout ("odd-r"): odd rows are visually shifted half a
  // cell to the right, so which diagonal neighbors are "above"/"below"
  // depends on whether point.y is even or odd.
  static const Point2D evenRowOffsets[6] = {
      {-1, -1}, {0, -1},  // up-left, up-right
      {-1, 0},  {1, 0},   // left, right
      {-1, 1},  {0, 1},   // down-left, down-right
  };
  static const Point2D oddRowOffsets[6] = {
      {0, -1}, {1, -1},  // up-left, up-right
      {-1, 0}, {1, 0},   // left, right
      {0, 1},  {1, 1},   // down-left, down-right
  };

  const bool oddRow = (point.y % 2 != 0);
  const Point2D* offsets = oddRow ? oddRowOffsets : evenRowOffsets;

  int count = 0;
  for (int i = 0; i < 6; ++i) {
    Point2D neighbor{point.x + offsets[i].x, point.y + offsets[i].y};
    if (world.Get(neighbor)) ++count;
  }
  return count;
  // end solution
}