#include "World.h"
void World::Resize(int size) {
  currentBufferId = 0;
  sideSize = size;
  buffer[0].clear();
  buffer[0].resize(size*size);
  buffer[1].clear();
  buffer[1].resize(size*size);
}
void World::SwapBuffers() {
  currentBufferId = (currentBufferId+1)%2;
}

