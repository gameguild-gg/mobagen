#include "Object.h"
#include <typeinfo>

template <class T> std::vector<T> Object::FindObjectsOfType() {
  auto typehash = typeid(T).hash_code();
  return std::vector<T>();
}