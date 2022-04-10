#ifndef MOBAGEN_VECTOR2_H
#define MOBAGEN_VECTOR2_H

#include <cmath>
#include <stdexcept>
#include "SDL.h"

// #TODO: create a math lib more efficient than this
#define DEG2RAD (float)((M_PI * 2) / 360)

struct Vector2 {
    float x;
    float y;

    Vector2() {
        x = 0;
        y = 0;
    };

    Vector2(float x, float y){
        this->x = x;
        this->y = y;
    };

    float sqrMagnitude() const{
        return x*x + y*y;
    }

    //unary operations
    Vector2 operator- () const { return Vector2(-x, -y); }

    //binary operations
    Vector2 operator- (const Vector2& rhs) const {
        return Vector2(x - rhs.x, y - rhs.y);
    }
    Vector2 operator+ (const Vector2& rhs) const {
        return Vector2(x + rhs.x, y + rhs.y);
    }
    Vector2 operator* (const float& rhs) const {
        return Vector2(x * rhs, y * rhs);
    }
    friend Vector2 operator* (const float& lhs, const Vector2& rhs) {
        return rhs * lhs;
    }
    Vector2 operator/ (const float& rhs) const {
        return Vector2(x / rhs, y / rhs);
    }
    bool operator!= (const Vector2& rhs) const {
        return (*this - rhs).sqrMagnitude() >= 1.0e-6;
    }
    bool operator== (const Vector2& rhs) const {
        return (*this - rhs).sqrMagnitude() < 1.0e-6;
    }

    //assignment operation
    Vector2& operator= (const Vector2& rhs)  {
        //Check for self-assignment
        if (this == &rhs)
            return *this;
        x = rhs.x;
        y = rhs.y;
        return *this;
    }

    //compound assignment operations
    Vector2& operator+= (const Vector2& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    Vector2& operator-= (const Vector2& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    Vector2& operator*= (const float& rhs) {
        x *= rhs;
        y *= rhs;
        return *this;
    }
    Vector2& operator/= (const float& rhs) {
        x /= rhs;
        y /= rhs;
        return *this;
    }

    //subscript operation
    float& operator[] (const int& i) {
        if (i < 0 || i > 1) throw std::out_of_range("Out of Vector2 range\n");
        return (i == 0) ? x : y;
    }
    const float& operator[] (const int& i) const {
        if (i < 0 || i > 1) throw std::out_of_range("Out of Vector2 range\n");
        return (i == 0) ? x : y ;
    }

    static Vector2 Rotate(Vector2 v, float degrees) {
        float sin = std::sinf(degrees * DEG2RAD);
        float cos = std::cosf(degrees * DEG2RAD);

        float tx = v.x;
        float ty = v.y;
        v.x = (cos * tx) - (sin * ty);
        v.y = (sin * tx) + (cos * ty);
        return v;
    }

    Vector2 Rotate(float degrees) const {
        Vector2 v;
        float sin = std::sinf(degrees * DEG2RAD);
        float cos = std::cosf(degrees * DEG2RAD);

        float tx = x;
        float ty = y;
        v.x = (cos * tx) - (sin * ty);
        v.y = (sin * tx) + (cos * ty);
        return v;
    }
};

#endif //MOBAGEN_VECTOR2_H
