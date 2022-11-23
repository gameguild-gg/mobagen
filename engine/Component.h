#ifndef MOBAGEN_ENGINE_COMPONENT_H_
#define MOBAGEN_ENGINE_COMPONENT_H_

#include "Object.h"
class Component: public Object {
 private:
  // todo: add active and enabled logic
  // todo: access the reference of the game object and the transform
 public:
  Component()= default;
  virtual void Start(){};
  virtual void OnGui(){};
  virtual void OnDraw(){};
  virtual void Update(){};
  // todo: get the transform and game object associated

//  template<typename T>
//      requires std::derived_from<T, Component>
};

#endif //MOBAGEN_ENGINE_COMPONENT_H_
