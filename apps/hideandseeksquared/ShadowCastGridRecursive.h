#ifndef MOBAGEN_SHADOWCASTGRIDRECURSIVE_H
#define MOBAGEN_SHADOWCASTGRIDRECURSIVE_H

#include "datastructures/Grid2D.h"
#include "math/Point2D.h"

enum class SquareType : uint8_t {
  Empty = 0,
  Wall = 1,
  Player = 2,
  Enemy = 3,
};

struct Square {
public:
  bool visible : 1;
  SquareType type : 2;

private:
  uint8_t _reserved : 5;
};

// every node is a enum that holds WALL, FLOOR, ENEMY, PLAYER
enum class Octant {
  NNE,
  ENE,
  ESE,
  SSE,
  SSW,
  WSW,
  WNW,
  NNW,
};

// slope is a datastructure holding 2 floats, max and min, representing the max and min slope of the current recursion level
struct Slope {
  float min;
  float max;
};

// parameters:
void ShadowCastGridRecursive(Grid2D<Square>& grid, Point2D& origin, int deepness, int maxDeepness, Octant octant, Slope slopeRange);

#endif  // MOBAGEN_SHADOWCASTGRIDRECURSIVE_H
