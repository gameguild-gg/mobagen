#ifndef MOBAGEN_HEURISTICS_H
#define MOBAGEN_HEURISTICS_H

#include <cstdint>
#include "WorldStateFwd.h"  // forward is needed here to avoid collisions

struct Heuristics {
public:
  inline uint16_t White() const { return white; };
  inline uint16_t Black() const { return black; };

private:
  uint16_t white = 0;
  uint16_t black = 0;

public:
  static Heuristics BoardAnalysis(WorldState* state);
};

#endif  // MOBAGEN_HEURISTICS_H
