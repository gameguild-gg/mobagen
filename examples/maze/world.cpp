#include "World.h"

World::World(Engine* pEngine, int size=11): GameObject(pEngine), sideSize(size) {
  data.clear();
  data.resize((size+1)*(size+1)*2);
}