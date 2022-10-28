#ifndef MAZEGENERATORBASE_H
#define MAZEGENERATORBASE_H

#include <string>

class World;

class MazeGeneratorBase {
public:
 MazeGeneratorBase() = default;
  virtual std::string GetName()=0;
 // Step should return true if it made changes in the world
  virtual bool Step(World * world) = 0;
  // Clears and resets all data from the generator
  virtual void Clear(World * world) = 0;
  virtual ~MazeGeneratorBase() = default;
};

#endif  // MAZEGENERATORBASE_H
