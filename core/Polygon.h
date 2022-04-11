#ifndef MOBAGEN_POLYGON_H
#define MOBAGEN_POLYGON_H

#include <vector>
#include "Vector2.h"
#include "Transform.h"

// naive approach to represent a polygon
struct Polygon {
public:
    Transform transform;
    std::vector<Vector2> points;

    // polygon points to be used in the draw functions
    std::vector<Vector2> getDrawablePoints();
};

#endif //MOBAGEN_POLYGON_H
