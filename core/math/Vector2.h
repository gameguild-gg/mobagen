#ifndef VECTOR2_H
#define VECTOR2_H

#include <cmath>
#include <stdexcept>
#include "SDL.h"
#include <concepts>
#include <type_traits>

// #TODO: create a math lib more efficient than this
#define DEG2RAD (float)((M_PI * 2) / 360)

template <typename T>
concept FloatType = std::is_floating_point_v<T>;

template <FloatType T> struct Vector2 {
  T x;
  T y;

  Vector2() : x(0), y(0){};
  Vector2(T x, T y) : x(x), y(y){};
  Vector2(const Vector2<T>& v) : x(v.x), y(v.y){};

  static Vector2<T> up() { return {0., -1.}; }
  static Vector2<T> down() { return {0., 1.}; }
  static Vector2<T> left() { return {-1., 0.}; }
  static Vector2<T> right() { return {1., 0.}; }
  static Vector2<T> zero() { return {0., 0.}; }
  static Vector2<T> identity() { return {1., 1.}; }

  // unary operations
  Vector2<T> operator-() const { return {-x, -y}; }
  Vector2<T> operator+() const { return {x, y}; }

  // binary operations
  Vector2<T> operator-(const Vector2& rhs) const { return {x - rhs.x, y - rhs.y}; }
  Vector2<T> operator+(const Vector2& rhs) const { return {x + rhs.x, y + rhs.y}; }
  Vector2<T> operator*(const T& rhs) const { return {x * rhs, y * rhs}; }
  friend Vector2<T> operator*(const T& lhs, const Vector2& rhs) { return {lhs * rhs.x, lhs * rhs.y}; }
  Vector2<T> operator/(const T& rhs) const { return {x / rhs, y / rhs}; }
  Vector2<T> operator/(const Vector2& rhs) const { return {x / rhs.x, y / rhs.y}; }
  bool operator!=(const Vector2<T>& rhs) const { return (*this - rhs).sqrMagnitude() >= 1.0e-6; };
  bool operator==(const Vector2<T>& rhs) const { return (*this - rhs).sqrMagnitude() < 1.0e-6; };

  // assignment operation
  Vector2<T>& operator=(Vector2<T> const& rhs) = default;

  // compound assignment operations
  Vector2<T>& operator+=(const Vector2<T>& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
  Vector2<T>& operator-=(const Vector2<T>& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }
  Vector2<T>& operator*=(const T& rhs) {
    x *= rhs;
    y *= rhs;
    return *this;
  }
  Vector2<T>& operator/=(const T& rhs) {
    x /= rhs;
    y /= rhs;
    return *this;
  }
  Vector2<T>& operator*=(const Vector2<T>& rhs) {
    x *= rhs.x;
    y *= rhs.y;
    return *this;
  }
  Vector2<T>& operator/=(const Vector2<T>& rhs) {
    x /= rhs.x;
    y /= rhs.y;
    return *this;
  }

  // subscript operation
  T& operator[](const int& i);
  const T& operator[](const int& i) const;

  static Vector2<T> Rotate(Vector2<T> v, T degrees) {
    auto sin = std::sin(degrees * DEG2RAD);
    auto cos = std::cos(degrees * DEG2RAD);

    auto tx = v.x;
    auto ty = v.y;
    v.x = (cos * tx) - (sin * ty);
    v.y = (sin * tx) + (cos * ty);
    return v;
  };
  Vector2<T> Rotate(T degrees) const {
    Vector2<T> v;
    auto sin = std::sin(degrees * DEG2RAD);
    auto cos = std::cos(degrees * DEG2RAD);

    auto tx = x;
    auto ty = y;
    v.x = (cos * tx) - (sin * ty);
    v.y = (sin * tx) + (cos * ty);
    return v;
  };
  static Vector2<T> Rotate(Vector2 v, Vector2 up) { return v.Rotate(up.getAngleDegree()); };
  Vector2<T> Rotate(Vector2<T> up) const { return Rotate(up.getAngleDegree()); };

  T getAngleDegree() const { return getAngleRadian() * 180 / (T)M_PI; };
  T static getAngleDegree(Vector2<T> v) { return v.getAngleDegree(); };

  T getAngleRadian() const { return atan2(x, -y); }  // todo: check if this is correct
  T static getAngleRadian(Vector2<T> v) { return v.getAngleRadian(); };

  static Vector2<T> Random(T start, T end);

  static Vector2<T> getVector2FromRadian(T radian);
  static Vector2<T> getVector2FromDegree(T degree);

  T sqrMagnitude() const { return x * x + y * y; }
  T getMagnitude() const { return sqrt(sqrMagnitude()); }
  static T getMagnitude(const Vector2<T>& vector) { return vector.getMagnitude(); }

  static T getDistance(const Vector2<T>& a, const Vector2<T>& b) { return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); };
  // todo: create distanceTo using the "this" value
  static T getSquaredDistance(const Vector2<T>& a, const Vector2<T>& b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); };

  static Vector2<T> normalized(const Vector2<T>& v) { return v.normalized(); };
  Vector2<T> normalized() const {
    auto magnitude = getMagnitude();

    // If the magnitude is not null
    if (magnitude > 0.)
      return Vector2<T>(x, y) / magnitude;
    else
      return {x, y};
  };
};

using Vector2f = Vector2<float>;

#endif  // VECTOR2_H
