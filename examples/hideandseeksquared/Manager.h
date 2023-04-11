#ifndef MOBAGEN_MANAGERHIDEANDSEEK_H
#define MOBAGEN_MANAGERHIDEANDSEEK_H

#include "engine/EngineForwards.h"
#include "scene/GameObject.h"
#include "datastructures/Grid2D.h"
#include "ShadowCastGridRecursive.h"

// make it be a component instead a gameobject
class Manager : public GameObject {
  int sideSize = 17;
  Grid2D<Square> grid;
  float enemyTickSize = 0.5f;
  float timeTimeRemaining = 0.5f;
  bool showHiddenObjects = true;

public:
  explicit Manager(Engine* pEngine);

  void Start() override;
  void OnGui(ImGuiContext* context) override;
  void OnDraw(SDL_Renderer* renderer) override;
  void Update(float deltaTime) override;

  // helper functions
  Point2D screenSpaceToGridIndex(ImVec2& pos);
  void Reset();

  // game logic
  void EnemyTick();
  void ShadowCast();
};

#endif  // MOBAGEN_MANAGER_H
