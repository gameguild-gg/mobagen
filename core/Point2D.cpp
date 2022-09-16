#include "Point2D.h"

bool Point2D::operator==(const Point2D &rhs) const {
    return this->y==rhs.y && this->x==rhs.x;
}
