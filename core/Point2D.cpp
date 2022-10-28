#include "Point2D.h"

bool Point2D::operator==(const Point2D& rhs) const {
    return y == rhs.y && x==rhs.x;
}

bool Point2D::operator!=(const Point2D& rhs) const {
  return x != rhs.x || y!=rhs.y;
}

Point2D &Point2D::operator=(const Point2D &rhs) {
  //Check for self-assignment
  if (this == &rhs)
    return *this;
  x = rhs.x;
  y = rhs.y;
  return *this;
}
Point2D Point2D::operator-(const Point2D& rhs) const {
  return {x - rhs.x, y - rhs.y};
}
