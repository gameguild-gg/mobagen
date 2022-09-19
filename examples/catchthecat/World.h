#ifndef WORLD_H
#define WORLD_H

#include "Cat.h"
#include "Catcher.h"
#include "GameObject.h"
#include "Point2D.h"
#include "Random.h"
#include <bitset>
#include <iostream>
#include <vector>

class World: GameObject {
 private:
  float timeBetweenAITicks=1;
  float timeForNextTick=1;
  bool catTurn = true;
  bool isSimulating= false;
  Point2D catPosition=Point2D(0,0);

  Cat *cat;
  Catcher *catcher;

  // false means empty
  // true means blocked
  std::vector<bool> worldState;

  // size of the side of the map
  int sideSize=0;
  // todo: optimization make the world state only use 16 Bytes.
  // hints on how to do it:
  // the world have 11x11 size, so it needs 121 bits to represent it. in other words we need 16 bytes to fully represent it. bit representation: 0 empty, 1 blocked.
  // to represent the cat position we need only one byte. 4 bits for X and another 4 for Y
  // create a structure holding one byte for cat position and 16 bytes for the blocked map.

  // clears the world
  void clearWorld();

  // directions
  static Point2D NE(const Point2D &p);
  static Point2D NW(const Point2D &p);
  static Point2D E(const Point2D &p);
  static Point2D W(const Point2D &p);
  static Point2D SE(const Point2D &p);
  static Point2D SW(const Point2D &p);

 public:
  explicit World(Engine* pEngine, int size=11);

  Point2D getCat();

  // the top left (x,y) is (-5,-5) the center is on (0,0);
  // get the content of a given point
  inline bool getContent(const Point2D& p) {
    return worldState[(p.y+sideSize/2)*(sideSize) + p.x + sideSize/2];
  }

  // the top left (x,y) is (-5,-5) the center is on (0,0);
  // get the content of a given
  inline bool getContent(const int8_t& x, const int8_t& y) {
    return worldState[(y+sideSize/2)*(sideSize) + x + sideSize/2];
  }

  // print the world state
  void print();

  // check if point is inside the world
  bool isValidPosition(const Point2D& p);

  static bool isNeighbor(const Point2D& p1, const Point2D &p2);

  void OnDraw(SDL_Renderer* renderer) override;
  void OnGui(ImGuiContext *context) override;
  void Update(float deltaTime) override;
};

#endif  // WORLD_H
