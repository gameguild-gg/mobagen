#ifndef LIFE_RULEBASE_H
#define LIFE_RULEBASE_H

// reference https://playgameoflife.com/info

#include "World.h"
#include "GameOfLifeTileSetEnum.h"
class RuleBase {
public:
  virtual ~RuleBase() = default;
  virtual void Step(World& world) = 0;
  virtual std::string GetName() = 0;
  virtual GameOfLifeTileSetEnum GetTileSet() = 0;
};

#endif  // MOBAGEN_RULEBASE_H
