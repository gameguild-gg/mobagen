#ifndef POINT2D_H
#define POINT2D_H
#include <string>

struct Point2D {
 public:
  Point2D(int x, int y) : x(x), y(y) {};
  Point2D() = default;
  int x, y;
  bool operator== (const Point2D& rhs) const;
  bool operator!= (const Point2D& rhs) const;
  Point2D& operator= (const Point2D& rhs);
  Point2D operator+ (const Point2D& rhs) const;
  Point2D operator- (const Point2D& rhs) const;

  inline Point2D Up() const{return *this + UP;};
  inline Point2D Left() const{return *this + LEFT;};
  inline Point2D Right() const{return *this + RIGHT;};
  inline Point2D Down() const{return *this + DOWN;};

  const static Point2D UP;
  const static Point2D DOWN;
  const static Point2D LEFT;
  const static Point2D RIGHT;
  std::string to_string();
};

#endif //POINT2D_H
