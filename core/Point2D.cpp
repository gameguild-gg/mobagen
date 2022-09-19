#include "Point2D.h"

bool Point2D::operator==(const Point2D &rhs) const {
    return this->y==rhs.y && this->x==rhs.x;
}

bool Point2D::operator!=(const Point2D &rhs) const {
  return this->x == rhs.x && this->y==rhs.y;
}

Point2D &Point2D::operator=(const Point2D &rhs) {
  //Check for self-assignment
  if (this == &rhs)
    return *this;
  x = rhs.x;
  y = rhs.y;
  return *this;
}
