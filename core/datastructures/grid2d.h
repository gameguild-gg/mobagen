#ifndef MOBAGEN_GRID2D_H
#define MOBAGEN_GRID2D_H

#include "../math/Point2D.h"

template <typename T>
struct Grid2D {
public:
  Grid2D() = default;
  Grid2D(int width, int height) : width(width), height(height) { data.reserve(width * height); }
  ~Grid2D() { data.clear(); }

  T& operator()(int x, int y) { return data[x + y * width]; }
  const T& operator()(int x, int y) const { return data[x + y * width]; }

  T& operator()(const Point2D& pos) { return data[pos.x + pos.y * width]; }
  const T& operator()(const Point2D& pos) const { return data[pos.x + pos.y * width]; }

  void Resize(int width, int height) {
    this->width = width;
    this->height = height;
    data.resize(width * height);
  }

  int width = 0;
  int height = 0;
  std::vector<T> data;
};

#endif  // MOBAGEN_GRID2D_H
