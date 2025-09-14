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
  if (point.x < world.SideSize() && point.y < world.SideSize()) { //iterate through the 6 possibilities
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
  if (neighborPoint.x >= world.SideSize()) { // if the point's neighbor is outside the range, we get specific
  
  }
  if (neighborPoint.y >= world.SideSize()) {

  }
  return neighbors;
}
