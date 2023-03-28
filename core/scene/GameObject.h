#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include "SceneForwards.h"
#include "../engine/EngineForwards.h"
#include "imgui.h"
#include "Transform.h"
#include "Object.h"

// todo: create a game list of game components of a game object
class GameObject : public Object {
public:
  // todo: make it private
  Transform transform;
  Engine* engine;

public:
  //  const Transform& Transform() const { return transform; }

  explicit GameObject(Engine* pEngine);

  // todo: move this to component. Revome from this class
  virtual void Start(){};
  virtual void OnGui(ImGuiContext* context){};
  virtual void OnDraw(SDL_Renderer* renderer){};
  virtual void Update(float deltaTime){};

  // make use of concepts here
  template <class T> std::vector<T> GetComponents() { return {}; };
  template <class T> std::vector<T> GetComponentsInChildren() { return {}; };
  template <class T> std::vector<T> GetComponentsInParent() { return {}; };
  template <class T> T* AddComponent() { return nullptr; };

  static GameObject* Find(std::string name) { return nullptr; };
};
#endif