#include "Polygon.h"

std::vector<Vector2> Polygon::getDrawablePoints() {
    std::vector<Vector2> ret;
    for(auto p : points) {
        auto scaled = Vector2(transform.scale.x*p.x, transform.scale.y*p.y);
        auto rotated = scaled.Rotate(transform.rotation.eulerAngle());
        auto displaced = rotated + transform.position;
        ret.push_back(displaced);
    }
    return ret;
}
