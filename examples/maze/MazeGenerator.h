#ifndef MOBAGEN_MAZEGENERATOR_H
#define MOBAGEN_MAZEGENERATOR_H

#include "MazeGeneratorBase.h"

// please do not use this one anymore. Move your code to the specific implementation.
// I am going to rename this interface soon to be a naive implementation.
class MazeGenerator: public MazeGeneratorBase {
 public:
  std::string GetName() override {return "deprecated";};
  // todo: in order to step work properly, you have to store your current exploration status in the MazeGenerator members
  // Steps should return true if it made changes in the world
  bool Step(World * world);
  // Clears and resets all data from the generator
  void Clear(World * world);
};

#endif
