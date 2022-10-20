#ifndef MOBAGEN_WORLD_H
#define MOBAGEN_WORLD_H

#include "GameObject.h"
#include "Point2D.h"
#include <vector>

class World: GameObject {
 private:
  int sideSize;

  std::vector<std::vector<bool>> data;
 public:
  explicit World(Engine* pEngine, int size);


};

#endif
