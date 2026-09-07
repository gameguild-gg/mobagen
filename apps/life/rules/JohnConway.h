#ifndef MOBAGEN_JOHNCONWAY_H
#define MOBAGEN_JOHNCONWAY_H
#include "../RuleBase.h"
#include "../fsm/State.h"
#include "../fsm/StateMachine.h"
#include <memory>
#include <string>

class JohnConway : public RuleBase {
public:
  JohnConway();
  ~JohnConway() override = default;
  std::string GetName() override { return "JohnConway"; }
  void Step(World& world) override;
  int CountNeighbors(World& world, Point2D point);
  GameOfLifeTileSetEnum GetTileSet() override { return GameOfLifeTileSetEnum::Square; };

private:
  // the shared state graph: every cell runs this same machine
  std::shared_ptr<State> alive;
  std::shared_ptr<State> dead;
  StateMachine machine;
};

#endif  // MOBAGEN_JOHNCONWAY_H
