#include "Polygon.h"

std::vector<Vector2> Polygon::getDrawablePoints() {
    std::vector<Vector2> ret;
    for(auto p : points)
        ret.push_back(p.Rotate(rotation)*scale + position);
    return ret;
}
