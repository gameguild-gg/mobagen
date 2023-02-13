//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
void HexagonGameOfLife::Step(World &world) {
  for(int lin = 0; lin<world.SideSize(); lin++){
    for(int col = 0; col<world.SideSize(); col++){
      auto point = Point2D{lin,col};
      auto neighs = CountNeighbors(world,point);
      auto isAlive = world.Get(point);
      if(isAlive) {
        if(neighs <=1 || neighs >=3)
          world.SetNext(point, false);
        else //if(neighs==2)
          world.SetNext(point, true);
      }
      else { // sprout only if have 2 neighbors
        if (neighs == 2)
          world.SetNext(point, true);
        else
          world.SetNext(point, false);
      }
    }
  }
}
int HexagonGameOfLife::CountNeighbors(World &world, Point2D point) {
  // if the line is even, it follows this
  //   (X-1,Y-1) (X,Y-1)
  //  (X-1,Y) (X,Y) (X+1,Y)
  //   (X-1,Y+1) (X,Y+1)
  if(point.y%2 == 0)
    return
      static_cast<int>(world.Get(point + Point2D::UP + Point2D::LEFT)) +
      static_cast<int>(world.Get(point + Point2D::UP)) +
      static_cast<int>(world.Get(point + Point2D::LEFT)) +
      static_cast<int>(world.Get(point + Point2D::RIGHT)) +
      static_cast<int>(world.Get(point + Point2D::DOWN + Point2D::LEFT)) +
      static_cast<int>(world.Get(point + Point2D::DOWN));

  // otherwise it will be
  //  (X,Y-1)     (X+1,Y-1)
  // (X-1,Y) (X,Y) (X+1,Y)
  //  (X,Y+1)     (X+1,Y+1)
  else
    return
      static_cast<int>(world.Get(point + Point2D::UP)) +
      static_cast<int>(world.Get(point + Point2D::UP + Point2D::RIGHT)) +
      static_cast<int>(world.Get(point + Point2D::LEFT)) +
      static_cast<int>(world.Get(point + Point2D::RIGHT)) +
      static_cast<int>(world.Get(point + Point2D::DOWN)) +
      static_cast<int>(world.Get(point + Point2D::DOWN+Point2D::RIGHT));
}
