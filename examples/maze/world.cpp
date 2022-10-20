#include "World.h"

World::World(Engine* pEngine, int size=11): GameObject(pEngine), sideSize(size) {
  data.clear();
}