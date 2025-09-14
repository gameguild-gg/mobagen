//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
void HexagonGameOfLife::Step(World& world) {
  //If Less than two Neighbors are alive, set point to dead
  //If 2 or 3 neighbors are alive, stay alive
  //if more than 3 neighbors are alive, set point to dead since overcrowded
  //if a dead point has 3 alive neighbors, becomes alive
}
int HexagonGameOfLife::CountNeighbors(World& world, Point2D point) {
  //Count neighbors

  return 0;
}
