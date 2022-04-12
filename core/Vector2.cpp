#include "Vector2.h"
#include "Random.h"

Vector2::Vector2() {
    x = 0;
    y = 0;
}

Vector2::Vector2(float x, float y) {
    this->x = x;
    this->y = y;
}

float Vector2::sqrMagnitude() const {
    return x * x + y * y;
}

float Vector2::getAngleDegree() const {
    return getAngleRadian() * 180 / (float)M_PI;

    // todo: if something looks weird. it would be probably bc we should just return the atan2
//    float rad = std::atan2(y,x);   // arcus tangent in radians
//    float deg = rad*180/(float)M_PI;  // converted to degrees
//    if (x<0) deg += 180;              // fixed mirrored angle of arctan
//    float eul = MathLib::normalize(270+deg, 0, 360);    // folded to [0,360) domain
//    return eul;
}

float Vector2::getAngleDegree(Vector2 v) {
    return v.getAngleDegree();
}

Vector2 Vector2::Rotate(float degrees) const {
    Vector2 v;
    auto sin = std::sin(degrees * DEG2RAD);
    auto cos = std::cos(degrees * DEG2RAD);

    auto tx = x;
    auto ty = y;
    v.x = (cos * tx) - (sin * ty);
    v.y = (sin * tx) + (cos * ty);
    return v;
}

Vector2 Vector2::operator-() const {
    return {-x, -y};
}

Vector2 Vector2::operator+() const {
    return {x, y};
}

Vector2 Vector2::operator-(const Vector2 &rhs) const {
    return {x - rhs.x, y - rhs.y};
}

Vector2 Vector2::operator+(const Vector2 &rhs) const {
    return {x + rhs.x, y + rhs.y};
}

Vector2 Vector2::operator*(const float &rhs) const {
    return {x * rhs, y * rhs};
}

Vector2 operator*(const float &lhs, const Vector2 &rhs) {
    return rhs * lhs;
}

Vector2 Vector2::operator/(const float &rhs) const {
    return {x / rhs, y / rhs};
}

bool Vector2::operator!=(const Vector2 &rhs) const {
    return (*this - rhs).sqrMagnitude() >= 1.0e-6;
}

bool Vector2::operator==(const Vector2 &rhs) const {
    return (*this - rhs).sqrMagnitude() < 1.0e-6;
}

Vector2 &Vector2::operator=(const Vector2 &rhs) {
    //Check for self-assignment
    if (this == &rhs)
        return *this;
    x = rhs.x;
    y = rhs.y;
    return *this;
}

Vector2 &Vector2::operator+=(const Vector2 &rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
}

Vector2 &Vector2::operator-=(const Vector2 &rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
}

Vector2 &Vector2::operator*=(const float &rhs) {
    x *= rhs;
    y *= rhs;
    return *this;
}

Vector2 &Vector2::operator/=(const float &rhs) {
    x /= rhs;
    y /= rhs;
    return *this;
}

float &Vector2::operator[](const int &i) {
    if (i < 0 || i > 1) throw std::out_of_range("Out of Vector2 range\n");
    return (i == 0) ? x : y;
}

const float &Vector2::operator[](const int &i) const {
    if (i < 0 || i > 1) throw std::out_of_range("Out of Vector2 range\n");
    return (i == 0) ? x : y ;
}

Vector2 Vector2::Rotate(Vector2 v, float degrees) {
    auto sin = std::sin(degrees * DEG2RAD);
    auto cos = std::cos(degrees * DEG2RAD);

    auto tx = v.x;
    auto ty = v.y;
    v.x = (cos * tx) - (sin * ty);
    v.y = (sin * tx) + (cos * ty);
    return v;
}

Vector2 Vector2::Rotate(Vector2 v, Vector2 up) {
    // todo: improve this!
    return v.Rotate(up.getAngleDegree());
}

Vector2 Vector2::Rotate(Vector2 up) const {
    return this->Rotate(up.getAngleDegree());
}

Vector2 Vector2::Random(float start, float end) {
    return {Random::Range(start,end),Random::Range(start,end)};
}

float Vector2::getAngleRadian() const {
    return atan2(x, -y);
}

float Vector2::getAngleRadian(Vector2 v) {
    return v.getAngleRadian();
}

Vector2 Vector2::getVector2FromRadian(const float radian) {
    return {cos(radian), sin(radian)};
}

Vector2 Vector2::getVector2FromDegree(const float degree) {
    float radian = degree * ((float)M_PI / 180.f);
    return getVector2FromRadian(radian);
}

float Vector2::getMagnitude() const {
    return sqrt(sqrMagnitude());
}

float Vector2::getMagnitude(const Vector2 &vector) {
    return vector.getMagnitude();
}

float Vector2::getDistance(const Vector2 &a, const Vector2 &b) {
    auto ab = a - b;
    return sqrt(ab.x * ab.x +
                ab.y * ab.y);
}

float Vector2::getSquaredDistance(const Vector2 &a, const Vector2 &b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

Vector2 Vector2::normalized(const Vector2 &vector) {
    return vector.normalized();
}

Vector2 Vector2::normalized() const {
    float magnitude = getMagnitude();

    //If the magnitude is not null
    if (magnitude > 0.)
        return Vector2(x,y) / magnitude;
    else
        return {x,y};
}

