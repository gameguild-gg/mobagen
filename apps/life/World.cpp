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
// todo: improve those set / get accessors
void World::SetNext(Point2D point, bool value) {
  if (point.x < 0) point.x += width;
  if (point.x >= width) point.x %= width;
  if (point.y < 0) point.y += height;
  if (point.y >= height) point.y %= height;
  auto index = point.y * width + point.x;
  auto size = width * height;
  if (index >= size) index %= size;
  buffer[(currentBufferId + 1) % 2][index] = value;
}
// todo: improve those set / get accessors
void World::SetCurrent(Point2D point, bool value) {
  if (point.x < 0) point.x += width;
  if (point.x >= width) point.x %= width;
  if (point.y < 0) point.y += height;
  if (point.y >= height) point.y %= height;
  auto index = point.y * width + point.x;
  auto size = width * height;
  if (index >= size) index %= size;
  buffer[currentBufferId % 2][index] = value;
}
// todo: improve those set / get accessors
bool World::Get(Point2D point) {
  if (point.x < 0) point.x += width;
  if (point.x >= width) point.x %= width;
  if (point.y < 0) point.y += height;
  if (point.y >= height) point.y %= height;
  auto index = point.y * width + point.x;
  auto size = width * height;
  if (index >= size) index %= size;
  return buffer[currentBufferId % 2][index];
}
void World::Randomize() {
  for (auto&& elem : buffer[0]) elem = (Random::Range(0, 1) != 0);

  for (int i = 0; i < buffer[0].size(); i++) buffer[1][i] = buffer[0][i];
}
