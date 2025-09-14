//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
void HexagonGameOfLife::Step(World& world) {
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
}
int HexagonGameOfLife::CountNeighbors(World& world, Point2D point) {
  //Count neighbors
  int neighbors = 0;
  Point2D neighborPoint = point; //take a reference of where the point is
  if (point.x-1 < world.SideSize() ==point.x && point.y-1 < world.SideSize() == point.y) { //iterate through the 6 possibilities
    neighborPoint.x = point.x + 1; //in front
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.x = neighborPoint.x - 1;
    neighborPoint.y = neighborPoint.y + 1; // above on the left
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.x = neighborPoint.x + 1; //above on the right
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.y = neighborPoint.y - 2; //below on the right
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.x = neighborPoint.x - 1; //below on the left
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.y = neighborPoint.y + 1;
    neighborPoint.x = neighborPoint.x - 1; // behind
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
  }
  if (neighborPoint.x+1 >= world.SideSize()) { // if the point's neighbor is outside the range, we get specific, in this case, if on the right edge
    //wrap to the left
    if (neighborPoint.y+1 >= world.SideSize()) {
      neighborPoint.y = 0;
    }
  neighborPoint.x = 0; //ahead
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.y = neighborPoint.y +1; //other side right
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    neighborPoint.x = point.x - 1; //behind
    neighborPoint.y = neighborPoint.y - 1;
    if (world.Get(neighborPoint) == true) {
      neighbors++;
    }
    if (neighborPoint.y-1 <= 0) { //check to see if we're in a corner on the left, if so, we wrap all the way around
      neighborPoint.y = world.SideSize()-1;
      if (world.Get(neighborPoint) == true) {
        neighbors++;
      } // Above left
      neighborPoint.y = point.y;
      neighborPoint.x = point.x - 1;
    }

    if (neighborPoint.y+1 >= world.SideSize()) { // if Y is at the end of the screen, wrap
      if (neighborPoint.x+1 >= world.SideSize()) {
        neighborPoint.x = 0;

      }
      neighborPoint.x = neighborPoint.x + 1;
      if (world.Get(neighborPoint) == true) {
        neighbors++;
      }
      neighborPoint.y = 0;

      if (world.Get(neighborPoint) == true) { // above
        neighbors++;
      }
    }
  }
  return neighbors;
}
