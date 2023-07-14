#ifndef MOBAGEN_CONCEPTS_H
#define MOBAGEN_CONCEPTS_H

#include <concepts>
#include <type_traits>
#include <functional>

template <typename T>
concept Hashable = requires(T a) {
  { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept Integral = std::is_integral<T>::value;

template <typename T>
concept Float = std::is_floating_point<T>::value;

template <typename T>
concept Sortable = requires(T a, T b) {
  { a < b } -> std::convertible_to<bool>;
};

#endif  // MOBAGEN_CONCEPTS_H
