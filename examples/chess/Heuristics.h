#ifndef MOBAGEN_HEURISTICS_H
#define MOBAGEN_HEURISTICS_H

#include <cstdint>
#include "WorldStateFwd.h"  // forward is needed here to avoid collisions
#include "Point2D.h"

struct Heuristics {
public:
  inline float Score() const { return score; };

private:
  // positive score means white is winning
  float score;

  static int materialScore(WorldState* state);
  static int distanceToCenter(Point2D location);

public:
  static Heuristics BoardAnalysis(WorldState* state);
};

#endif  // MOBAGEN_HEURISTICS_H
