#ifndef LIFE_WORLD_H
#define LIFE_WORLD_H

#include "math/Point2D.h"
#include <vector>

struct World {
private:
  // double buffer approach to avoid memory reallocation
  std::vector<bool> buffer[2];
  int currentBufferId;
  int width;
  int height;
  inline std::vector<bool>& currentBuffer() { return buffer[currentBufferId % 2]; }
  inline std::vector<bool>& nextBuffer() { return buffer[(currentBufferId + 1) % 2]; }

public:
  inline const int& Width() const { return width; };
  inline const int& Height() const { return height; };
  // square grids (visual app)
  void Resize(int sideSize);
  // rectangular grids (formal tests): C columns x L lines
  void Resize(int columns, int lines);
  // flips the buffers, promoting the next generation to current. Called by
  // whoever drives the simulation (the demo app's Manager::step or the
  // life-tests runner) right AFTER a rule Step returns - never from inside a
  // rule, which must only write via SetNext.
  void SwapBuffers();
  // todo: make it follow the standard at() function that returns the exactly element
  bool Get(Point2D point);
  // todo: make it follow the standard at() function that returns the exactly element
  void SetNext(Point2D point, bool value);
  void SetCurrent(Point2D point, bool value);
  void Randomize();
};

#endif  // MOBAGEN_WORLD_H
