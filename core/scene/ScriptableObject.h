#ifndef MOBAGEN_SCRIPTABLEOBJECT_H
#define MOBAGEN_SCRIPTABLEOBJECT_H

#include "../engine/Engine.h"
#include "Object.h"
// todo: this should be a scene object?
class ScriptableObject : public Object {
public:
  explicit ScriptableObject() : Object() { Engine::GetInstance()->AddScriptableObject(this); };

  // messages
  virtual void Awake(){};
  virtual void OnDestroy(){};
  virtual void OnDisable(){};
  virtual void OnEnable(){};
  virtual void OnValidate(){};
  virtual void Reset(){};

  virtual void OnGui(ImGuiContext* context){};  // todo: if a scriptable object has a gui, it should be drawn in the editor this will require a huge
                                                // refactor of the editor

  // methods
  template <typename T>
  requires std::is_base_of<ScriptableObject, T>::value static T* CreateInstance(Engine* engine) {  // todo: remove dependency of engine
    return new T(engine);
  }
};

#endif  // MOBAGEN_SCRIPTABLEOBJECT_H
