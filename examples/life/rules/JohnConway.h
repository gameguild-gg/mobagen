#ifndef MOBAGEN_JOHNCONWAY_H
#define MOBAGEN_JOHNCONWAY_H
#include "../RuleBase.h"

class JohnConway: public RuleBase {
 public:
  explicit JohnConway()=default;
  ~JohnConway() override =default;
  std::string GetName() override{return "JohnConway";}
  void Step(World& world) override;
  int CountNeighbors(World& world, Point2D point);
  GameOfLifeTileSetEnum GetTileSet() override{return GameOfLifeTileSetEnum::Square;};
};

#endif  // MOBAGEN_JOHNCONWAY_H
