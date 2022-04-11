#ifndef MOBAGEN_VECTOR2_H
#define MOBAGEN_VECTOR2_H

#include <cmath>
#include <stdexcept>
#include "SDL.h"
#include "MathLib.h"

// #TODO: create a math lib more efficient than this
#define DEG2RAD (float)((M_PI * 2) / 360)

struct Vector2 {
    float x;
    float y;

    Vector2() ;
    Vector2(float x, float y);
    float sqrMagnitude() const;

    //unary operations
    Vector2 operator- () const;
    Vector2 operator+ () const;

    //binary operations
    Vector2 operator- (const Vector2& rhs) const;
    Vector2 operator+ (const Vector2& rhs) const;
    Vector2 operator* (const float& rhs) const;
    friend Vector2 operator* (const float& lhs, const Vector2& rhs);
    Vector2 operator/ (const float& rhs) const;
    bool operator!= (const Vector2& rhs) const;
    bool operator== (const Vector2& rhs) const;

    //assignment operation
    Vector2& operator= (const Vector2& rhs);

    //compound assignment operations
    Vector2& operator+= (const Vector2& rhs);
    Vector2& operator-= (const Vector2& rhs);
    Vector2& operator*= (const float& rhs);
    Vector2& operator/= (const float& rhs);

    //subscript operation
    float& operator[] (const int& i);
    const float& operator[] (const int& i) const;

    static Vector2 Rotate(Vector2 v, float degrees);
    Vector2 Rotate(float degrees) const;

    static Vector2 Rotate(Vector2 v, Vector2 up);
    Vector2 Rotate(Vector2 up) const;

    float eulerAngle() const;
    float static eulerAngle(Vector2 v);
};

#endif //MOBAGEN_VECTOR2_H
