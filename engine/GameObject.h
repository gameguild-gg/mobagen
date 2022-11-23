#ifndef MOBAGEN_ENGINE_GAMEOBJECT_H_
#define MOBAGEN_ENGINE_GAMEOBJECT_H_

#include "Object.h"
#include "Transform.h"
// a game object attaches components/behaviors
class GameObject: public Object {
 private:
  Transform _transform;
 public:
  const Transform& transform() const { return _transform; }

 public:
  template<class T>
  std::vector<T> GetComponents();

  template<class T>
  std::vector<T> GetComponentsInChildren();

  template<class T>
  std::vector<T> GetComponentsInParent();

  template<class T>
  T* AddComponent();

  static GameObject* Find(std::string name);

  // todo: tag, layer, sendmessage
};

#endif //MOBAGEN_ENGINE_GAMEOBJECT_H_
