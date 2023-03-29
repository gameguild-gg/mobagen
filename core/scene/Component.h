#ifndef MOBAGEN_ENGINE_COMPONENT_H_
#define MOBAGEN_ENGINE_COMPONENT_H_

#include "SceneForwards.h"
#include "Object.h"
#include "../error/NotImplemented.h"
#include "GameObject.h"
#include "SendMessageOptions.h"

class Component : public Object {
  Transform* transform;
  GameObject* gameObject;
//  Component *GetComponent(Type type);
  template <class T> T GetComponent(){throw NotImplemented();};
//  bool TryGetComponent(System.Type type, Component* component){throw NotImplemented();};
  template <class t> bool TryGetComponent(Component* component){throw NotImplemented();};
  Component* GetComponent(std::string type){throw NotImplemented();};
  Component* GetComponentInChildren(std::string type, bool includeInative = false){throw NotImplemented();};
  Component* GetComponentInParent(std::string type, bool includeInative = false){throw NotImplemented();};
  //Component* GetComponentInParent(Type type){throw NotImplemented();};
  template <class T> T GetComponentInChildren(bool includeInative = false){throw NotImplemented();};
  std::vector<Component*> GetComponentsInChildren(std::string type, bool includeInative = false){throw NotImplemented();};
  //std::vector<Component*> GetComponentsInChildren(Type type, bool includeInative = false){throw NotImplemented();};
  template <class T> std::vector<T> GetComponentsInChildren(bool includeInative = false){throw NotImplemented();};
  std::vector<Component*> GetComponentsInParent(std::string type, bool includeInative = false){throw NotImplemented();};
  //std::vector<Component*> GetComponentsInParent(Type type, bool includeInative = false){throw NotImplemented();};
  template <class T> std::vector<T> GetComponentsInParent(bool includeInative = false){throw NotImplemented();};
  std::vector<Component*> GetComponents(std::string type){throw NotImplemented();};
  //std::vector<Component*> GetComponents(Type type){throw NotImplemented();};
  template <class T> std::vector<T> GetComponents(){throw NotImplemented();};

  std::string tag(){return gameObject->tag;};
  void tag(std::string value){gameObject->tag = value;};
  bool CompareTag(std::string tag){return gameObject->CompareTag(tag);};
  void SendMessageUpwards(std::string methodName){throw NotImplemented();};
  void SendMessageUpwards(std::string methodName, void* value = nullptr, SendMessageOptions options = SendMessageOptions::RequireReceiver){throw NotImplemented();};
  void SendMessageUpwards(std::string methodName, SendMessageOptions options = SendMessageOptions::RequireReceiver, void* value = nullptr){throw NotImplemented();};
  void SendMessage(std::string methodName){throw NotImplemented();};
  void SendMessage(std::string methodName, void* value = nullptr, SendMessageOptions options = SendMessageOptions::RequireReceiver){throw NotImplemented();};
  void SendMessage(std::string methodName, SendMessageOptions options = SendMessageOptions::RequireReceiver, void* value = nullptr){throw NotImplemented();};
  void BroadcastMessage(std::string methodName){throw NotImplemented();};
  void BroadcastMessage(std::string methodName, void* parameter = nullptr, SendMessageOptions options = SendMessageOptions::RequireReceiver){throw NotImplemented();};
  void BroadcastMessage(std::string methodName, SendMessageOptions options = SendMessageOptions::RequireReceiver, void* parameter = nullptr){throw NotImplemented();};

  // todo: add active and enabled logic
  // todo: access the reference of the game object and the transform
public:
  Component() = default;
  virtual void Start(){};
  virtual void OnGui(){};
  virtual void OnDraw(){};
  virtual void Update(){};
  // todo: get the transform and game object associated

  //  template<typename T>
  //      requires std::derived_from<T, Component>
};

#endif  // MOBAGEN_ENGINE_COMPONENT_H_
