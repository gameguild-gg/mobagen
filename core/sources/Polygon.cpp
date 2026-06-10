#include "Polygon.h"
#include "Renderer2D.h"
#include "math/ColorT.h"

std::vector<Vector2f> Polygon::getDrawablePoints(const Transform& transform) {
  std::vector<Vector2f> ret;
  for (auto p : points) {
    auto scaled = Vector2f(transform.scale.x * p.x, transform.scale.y * p.y);
    auto rotated = scaled.Rotate(transform.rotation.getAngleDegree());
    auto displaced = rotated + transform.position;
    ret.push_back(displaced);
  }
  return ret;
}

void Polygon::Draw(Renderer2D& r, const Transform& transform, const Color32& color) {
  r.SetDrawColor(color.r, color.g, color.b, 255);
  auto drawablePoints = getDrawablePoints(transform);
  for (size_t i = 0; i < drawablePoints.size(); i++) {
    size_t other = i + 1;
    if (other == drawablePoints.size()) other = 0;
    r.DrawLine(drawablePoints[i].x, drawablePoints[i].y,
               drawablePoints[other].x, drawablePoints[other].y);
  }
}

void Polygon::DrawLine(Renderer2D& r, const Vector2f& v1, const Vector2f& v2, const Color32& color) {
  r.SetDrawColor(color.r, color.g, color.b, 255);
  r.DrawLine(v1.x, v1.y, v2.x, v2.y);
}

void Polygon::Draw(Renderer2D& r, const Vector2f& position, const Vector2f& scale, const Vector2f& rotation, const Color32& color) {
  Transform transform = {position, scale, rotation};
  Draw(r, transform, color);
}

Circle::Circle(int sample) {
  for (int i = 0; i < sample; i++) {
    Vector2f point = Vector2f::up().Rotate(360.f * (float)i / (float)sample);
    points.push_back(point);
  }
}

Square::Square() {
  points.push_back(Vector2f::up().Rotate(45));
  points.push_back(Vector2f::up().Rotate(135));
  points.push_back(Vector2f::up().Rotate(225));
  points.push_back(Vector2f::up().Rotate(315));
}

Hexagon::Hexagon() {
  points.push_back(Vector2f::up().Rotate(0));
  points.push_back(Vector2f::up().Rotate(60));
  points.push_back(Vector2f::up().Rotate(120));
  points.push_back(Vector2f::up().Rotate(180));
  points.push_back(Vector2f::up().Rotate(240));
  points.push_back(Vector2f::up().Rotate(300));
}
