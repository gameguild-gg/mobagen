#ifndef MOBAGEN_EXAMPLES_LIFE_RULES_HEXAGONGAMEOFLIFE_H_
#define MOBAGEN_EXAMPLES_LIFE_RULES_HEXAGONGAMEOFLIFE_H_

#include "../RuleBase.h"
#include "../fsm/State.h"
#include "../fsm/StateMachine.h"
#include <memory>
#include <string>

class HexagonGameOfLife : public RuleBase {
public:
  HexagonGameOfLife();
  ~HexagonGameOfLife() override = default;
  std::string GetName() override { return "Hexagon"; }
  void Step(World& world) override;
  int CountNeighbors(World& world, Point2D point);
  GameOfLifeTileSetEnum GetTileSet() override { return GameOfLifeTileSetEnum::Hexagon; };

private:
  // the shared state graph: every cell runs this same machine
  std::shared_ptr<State> alive;
  std::shared_ptr<State> dead;
  StateMachine machine;
};

#endif  // MOBAGEN_EXAMPLES_LIFE_RULES_HEXAGONGAMEOFLIFE_H_
