#include "World.h"
#include "Random.h"

void World::Resize(int size) { Resize(size, size); }
void World::Resize(int columns, int lines) {
  currentBufferId = 0;
  width = columns;
  height = lines;
  buffer[0].clear();
  buffer[0].resize(columns * lines);
  buffer[1].clear();
  buffer[1].resize(columns * lines);
}
void World::SwapBuffers() {
  currentBufferId = (currentBufferId + 1) % 2;
  for (int i = 0; i < buffer[currentBufferId].size(); i++) buffer[(currentBufferId + 1) % 2][i] = buffer[currentBufferId][i];
}

int World::Index(Point2D point) const {
  int x = ((point.x % width) + width) % width;
  int y = ((point.y % height) + height) % height;
  return y * width + x;
}

void World::SetNext(Point2D point, bool value) {
  buffer[(currentBufferId + 1) % 2][Index(point)] = value;
}
void World::SetCurrent(Point2D point, bool value) {
  buffer[currentBufferId % 2][Index(point)] = value;
}
bool World::Get(Point2D point) {
  return buffer[currentBufferId % 2][Index(point)];
}

void World::Randomize() {
  for (auto&& elem : buffer[0]) elem = (Random::Range(0, 1) != 0);

  for (int i = 0; i < buffer[0].size(); i++) buffer[1][i] = buffer[0][i];
}