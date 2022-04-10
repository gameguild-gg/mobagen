#ifndef MOBAGEN_POLYGON_H
#define MOBAGEN_POLYGON_H

#include <vector>
#include "Vector2.h"

// naive approach to represent a polygon
struct Polygon {
    float rotation;
    float scale;
    Vector2 position;
    std::vector<Vector2> points;

    // to be used in the draw functions
    std::vector<Vector2> getDrawablePoints();
};

#endif //MOBAGEN_POLYGON_H
