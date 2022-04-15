#ifndef MOBAGEN_POLYGON_H
#define MOBAGEN_POLYGON_H

#include <vector>
#include "Vector2.h"
#include "Transform.h"
#include "Vector3.h"

// naive approach to represent a polygon
struct Polygon {
public:
    Polygon() = default;
    explicit Polygon(std::vector<Vector2> points):points(std::move(points)){};
    std::vector<Vector2> points;

    // polygon points to be used in the draw functions
    std::vector<Vector2> getDrawablePoints(const Transform& transform);

    void Draw(SDL_Renderer* renderer, const Transform& transform, const Vector3& color);
    void Draw(SDL_Renderer* renderer, const Vector2& position, const Vector2& scale, const Vector2& rotation, const Vector3& color);

    static void DrawLine(SDL_Renderer* renderer, const Vector2& v1, const Vector2& v2, const Vector3& color);
};

struct Circle: Polygon{
    explicit Circle(int sample);
};

struct Square: Polygon{
    explicit Square();
};

#endif //MOBAGEN_POLYGON_H
