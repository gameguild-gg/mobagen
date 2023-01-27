#ifndef LIFE_RULEBASE_H
#define LIFE_RULEBASE_H

#include "World.h"
class RuleBase {
  virtual bool Run(const World& world);
};

#endif  // MOBAGEN_RULEBASE_H
