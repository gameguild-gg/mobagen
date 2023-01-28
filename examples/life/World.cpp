#include "World.h"
#include "Random.h"
void World::Resize(int size) {
  currentBufferId = 0;
  sideSize = size;
  buffer[0].clear();
  buffer[0].resize(size*size);
  buffer[1].clear();
  buffer[1].resize(size*size);

  for(auto && elem : buffer[0])
    elem = (Random::Range(0,1) != 0);

  for(int i = 0; i<buffer[0].size(); i++)
    buffer[1][i] = buffer[0][i];
}
void World::SwapBuffers() {
  currentBufferId = (currentBufferId+1)%2;
  for(int i = 0; i<buffer[currentBufferId].size(); i++)
    buffer[(currentBufferId+1)%2][i] = buffer[currentBufferId][i];
}

