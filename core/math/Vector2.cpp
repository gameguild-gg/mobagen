#include "Vector2.h"
#include "../Random.h"

template<FloatType T> 
T& Vector2<T>::operator[](const int& i) {
  if (i < 0 || i > 1) throw std::out_of_range("Out of Vector2<T> range\n");
  return (i == 0) ? x : y;
}

template<FloatType T> 
const T& Vector2<T>::operator[](const int& i) const {
  if (i < 0 || i > 1) throw std::out_of_range("Out of Vector2<T> range\n");
  return (i == 0) ? x : y;
}

template<FloatType T> 
Vector2<T> Vector2<T>::Random(T start, T end) { return {Random::Range(start, end), Random::Range(start, end)}; }

template<FloatType T> 
Vector2<T> Vector2<T>::getVector2FromRadian(const T radian) { return {cos(radian), sin(radian)}; }

template<FloatType T> 
Vector2<T> Vector2<T>::getVector2FromDegree(const T degree) {
  T radian = degree * ((T)M_PI / 180);
  return getVector2FromRadian(radian);
}
