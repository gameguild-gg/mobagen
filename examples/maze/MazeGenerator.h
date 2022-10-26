#ifndef MOBAGEN_MAZEGENERATOR_H
#define MOBAGEN_MAZEGENERATOR_H

#include "MazeGeneratorBase.h"

// please do not use this one anymore. Move your code to the specific implementation.
// I am going to rename this interface soon to be a naive implementation.
class MazeGenerator: public MazeGeneratorBase {
 public:
  // todo: in order to step work properly, you have to store your current exploration status in the MazeGenerator members
  void Step(World * world);
  void Clear(World * world);
};

#endif
