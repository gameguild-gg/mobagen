//
// Created by atolstenko on 2/9/2023.
//

#include "HexagonGameOfLife.h"
#include <iostream>
void HexagonGameOfLife::Step(World& world) {
  for (int y = 0; y < world.SideSize(); ++y) {
    for (int x = 0; x < world.SideSize(); ++x)
    {
      int neighbors = CountNeighbors(world,Point2D(x,y));
      if (neighbors==2)//(neighbors==2&&world.Get(Point2D(x,y)))||
      {
        world.SetNext(Point2D(x,y), true);
      }
      else
      {
        world.SetNext(Point2D(x,y), false);
      }
    }
  }

  world.SwapBuffers();
}
int HexagonGameOfLife::CountNeighbors(World& world, Point2D point) {
  int count;
//assume 1,1
  bool shift =point.y%2!=0;
  if (shift){//assume 1,1
    count= world.Get(point.Up())+//1,0
           world.Get(point.Right().Up())+//2,0
           world.Get(point.Right())+//2,1
           world.Get(point.Right().Down())+//2,2
           world.Get(point.Down())+//1,2
           world.Get(point.Left());//0,1


  }
  else{//assume 2,2
    count = world.Get(point.Up().Left())+//1,1
           world.Get(point.Up())+//2,1
           world.Get(point.Right())+//3,2
           world.Get(point.Down())+//2,3
           world.Get(point.Down().Left())+//1,3
           world.Get(point.Left());//1,2
  }

  return count;


}
