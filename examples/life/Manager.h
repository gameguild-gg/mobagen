#ifndef LIFE_MANAGER_H
#define LIFE_MANAGER_H

#include "GameObject.h"
#include "World.h"

class Manager: GameObject {
  int sideSize=128;
  World world;

 public:
  explicit Manager(Engine* pEngine);

  void Start() override;
  void OnGui(ImGuiContext *context) override;
  void OnDraw(SDL_Renderer* renderer) override;
  void Update(float deltaTime) override;
};

#endif  // MOBAGEN_MANAGER_H
