#ifndef MOBAGEN_CONCEPTS_H
#define MOBAGEN_CONCEPTS_H

#include <concepts>
#include <type_traits>
#include <functional>

template <typename T>
concept HashableConcept = requires(T a) {
  { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept IntegralConcept = std::integral<T>;

template <typename T>
concept FloatConcept = std::floating_point<T>;

template <typename T>
concept NumberConcept = std::integral<T> || std::floating_point<T>;

template <typename T>
concept SortableConcept = requires(T a, T b) {
  { a < b } -> std::convertible_to<bool>;
};

#endif  // MOBAGEN_CONCEPTS_H
