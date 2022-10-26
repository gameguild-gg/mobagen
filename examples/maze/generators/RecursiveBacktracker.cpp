#include "RecursiveBacktracker.h"
#include "../World.h"
#include "Random.h"
void RecursiveBacktracker::Step(World* world) {

}
void RecursiveBacktracker::Clear(World* world) {
  visited.clear();
  stack.clear();
  auto sideOver2 = world->GetSize()/2;

  start = {Random::Range(-sideOver2, sideOver2), Random::Range(-sideOver2, sideOver2)};
  current = start;
  for(int i=-sideOver2;i<=sideOver2;i++){
    for(int j=-sideOver2;j<=sideOver2;j++){
      visited[i][j] = false;
    }
  }
}
