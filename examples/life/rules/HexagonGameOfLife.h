#ifndef MOBAGEN_EXAMPLES_LIFE_RULES_HEXAGONGAMEOFLIFE_H_
#define MOBAGEN_EXAMPLES_LIFE_RULES_HEXAGONGAMEOFLIFE_H_

#include "../RuleBase.h"
class HexagonGameOfLife: public RuleBase {
 public:
  explicit HexagonGameOfLife()=default;
  ~HexagonGameOfLife() override =default;
  std::string GetName() override{return "Hexagon";}
  void Step(World& world) override;
  int CountNeighbors(World& world, Point2D point);
  GameOfLifeTileSetEnum GetTileSet() override{return GameOfLifeTileSetEnum::Hexagon;};
};

#endif //MOBAGEN_EXAMPLES_LIFE_RULES_HEXAGONGAMEOFLIFE_H_
