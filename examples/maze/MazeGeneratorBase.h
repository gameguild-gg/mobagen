#ifndef MAZEGENERATORBASE_H
#define MAZEGENERATORBASE_H

class World;

class MazeGeneratorBase {
public:
  virtual void Step(World * world) = 0;
  virtual void Clear(World * world) = 0;
  virtual ~MazeGeneratorBase() = default;
};

#endif  // MAZEGENERATORBASE_H
