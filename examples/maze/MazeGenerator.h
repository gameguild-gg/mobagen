#ifndef MOBAGEN_MAZEGENERATOR_H
#define MOBAGEN_MAZEGENERATOR_H

class World;

class MazeGenerator {
 public:
  // todo: in order to step work properly, you have to store your current
  // exploration status in the MazeGenerator members
  void Generate(World * world);
};

#endif
