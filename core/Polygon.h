#ifndef POLYGON_H
#define POLYGON_H

#include <vector>
#include "math/Vector2.h"
#include "scene/Transform.h"
#include "math/Vector3.h"
#include "math/ColorT.h"

// naive approach to represent a polygon
struct Polygon {
public:
  Polygon() = default;
  explicit Polygon(std::vector<Vector2f> points) : points(std::move(points)){};
  std::vector<Vector2f> points;

  // polygon points to be used in the draw functions
  std::vector<Vector2f> getDrawablePoints(const Transform& transform);

  void Draw(SDL_Renderer* renderer, const Transform& transform, const Color32& color);
  void Draw(SDL_Renderer* renderer, const Vector2f& position, const Vector2f& scale, const Vector2f& rotation, const Color32& color);

  static void DrawLine(SDL_Renderer* renderer, const Vector2f& v1, const Vector2f& v2, const Color32& color);
};

struct Circle : Polygon {
  explicit Circle(int sample);
};

struct Square : Polygon {
  explicit Square();
};

struct Hexagon : Polygon {
  explicit Hexagon();
};

#endif  // POLYGON_H
