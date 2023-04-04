#include "Polygon.h"
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

void Polygon::Draw(SDL_Renderer* renderer, const Transform& transform, const Color32& color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  auto drawablePoints = getDrawablePoints(transform);
  for (int i = 0; i < drawablePoints.size(); i++) {
    int other = i + 1;
    if (other == drawablePoints.size()) other = 0;
    SDL_RenderDrawLine(renderer, (int)drawablePoints[i].x, (int)drawablePoints[i].y, (int)drawablePoints[other].x, (int)drawablePoints[other].y);
  }
}

void Polygon::DrawLine(SDL_Renderer* renderer, const Vector2f& v1, const Vector2f& v2, const Color32& color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  SDL_RenderDrawLine(renderer, (int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y);
}

void Polygon::Draw(SDL_Renderer* renderer, const Vector2f& position, const Vector2f& scale, const Vector2f& rotation, const Color32& color) {
  Transform transform = {position, scale, rotation};
  Draw(renderer, transform, color);
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
