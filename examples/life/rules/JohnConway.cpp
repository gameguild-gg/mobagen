#include "JohnConway.h"

// Reference: https://playgameoflife.com/info
void JohnConway::Step(World& world) {
  //If Less than two Neighbors are alive, set point to dead
  //If 2 or 3 neighbors are alive, stay alive
  //if more than 3 neighbors are alive, set point to dead since overcrowded
  //if a dead point has 3 alive neighbors, becomes alive
  for (int y = 0; y < world.SideSize(); ++y) {
    for (int x = 0; x < world.SideSize(); ++x) { //iterating through the double layered Vector of Bools
      Point2D point(x, y);
      if (world.Get(point)) {
        world.SetNext(point, CountNeighbors(world, point) ==2 || CountNeighbors(world, point) == 3);
        //set(y, x, countNeighbors(y, x) == 2 || countNeighbors(y, x) == 3);
      }
      else {

        world.SetNext(point, CountNeighbors(world, point) == 3);
        //world.SetNext(x, y, CountNeighbors(world, point) == 3);
      }
    }
  }

  world.SwapBuffers(); //Then once this has all worked out, swap buffers
}

int JohnConway::CountNeighbors(World& world, Point2D point) {
  //Count neighbors
  int neighbors = 0;
  for (auto i = point.x-1; i <=point.x+1 ; ++i) {
    for (auto j = point.y-1; j <=point.y+1 ; ++j) {
      if (i>=0 && i<world.SideSize() && j>=0 && j<world.SideSize()) {
        Point2D neighborPoint(i,j);
        if (world.Get(neighborPoint) == true && point != neighborPoint) {
          neighbors++;
        }
      }
      else {
        Point2D neighborPoint(i,j);
        if (i<=0) {
          neighborPoint.x = world.SideSize() - 1;
          if (world.Get(neighborPoint) == true) {
            neighbors++;
          }
        }
        if (i>=world.SideSize()) {
          neighborPoint.x = 0;
          if (world.Get(neighborPoint) == true) {
            neighbors++;
          }
        }
        if (j<=0) {
          neighborPoint.y = world.SideSize() - 1;
          if (world.Get(neighborPoint) == true) {
            neighbors++;
          }
        }
        if (j>=world.SideSize()) {
          neighborPoint.y = 0;
          if (world.Get(neighborPoint) == true) {
            neighbors++;
          }
        }
      }
    }
  }
  return neighbors;
}
