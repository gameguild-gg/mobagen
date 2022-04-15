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

    // directions
    // todo: probably this should be put outside the struct to reduce memory used by function pointers
    static Vector2 up(){return {0., -1.};}
    static Vector2 down(){return {0., 1.};}
    static Vector2 left(){return {-1., 0.};}
    static Vector2 right(){return {1., 0.};}
    static Vector2 zero(){return {0., 0.};}

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

    // todo: make "const Vector2& vector"
    static Vector2 Rotate(Vector2 v, float degrees);
    Vector2 Rotate(float degrees) const;

    static Vector2 Rotate(Vector2 v, Vector2 up);
    Vector2 Rotate(Vector2 up) const;

    float getAngleDegree() const;
    float static getAngleDegree(Vector2 v);

    float getAngleRadian() const;
    float static getAngleRadian(Vector2 v);

    static Vector2 Random(float start, float end);

    static Vector2 getVector2FromRadian(float radian);
    static Vector2 getVector2FromDegree(float degree);

    float sqrMagnitude() const;
    float getMagnitude() const;
    static float getMagnitude(const Vector2& vector);

    static float getDistance(const Vector2& a, const Vector2& b);
    // todo: create distanceTo using the "this" value
    static float getSquaredDistance(const Vector2& a, const Vector2& b);

    static Vector2 normalized(const Vector2& vector);
    Vector2 normalized() const;
};



#endif //MOBAGEN_VECTOR2_H
