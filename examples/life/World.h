#ifndef LIFE_WORLD_H
#define LIFE_WORLD_H

#include "Point2D.h"
#include <vector>

struct World {
 private:
  // double buffer approach to avoid memory reallocation
  std::vector<bool> buffer[2];
  int currentBufferId;
  int sideSize;
  inline std::vector<bool>& currentBuffer() {return buffer[currentBufferId%2];}
  inline std::vector<bool>& nextBuffer() {return buffer[(currentBufferId+1)%2];}
 public:
  inline const int& SideSize() const {return sideSize;};
  void Resize(int sideSize);
  // to be called at the end of the frame
  void SwapBuffers();
  // todo: make it follow the standard at() function that returns the exactly element
  bool Get(Point2D point);
  // todo: make it follow the standard at() function that returns the exactly element
  void SetNext(Point2D point, bool value);
  void SetCurrent(Point2D point, bool value);
  void Randomize();
};

#endif  // MOBAGEN_WORLD_H
