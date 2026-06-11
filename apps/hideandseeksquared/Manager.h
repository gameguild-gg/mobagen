#ifndef MOBAGEN_MANAGERHIDEANDSEEK_H
#define MOBAGEN_MANAGERHIDEANDSEEK_H

#include <glm/glm.hpp>
#include "imgui.h"
#include "datastructures/Grid2D.h"
#include "ShadowCastGridRecursive.h"

// DOD-ified: plain state container + system methods.
// No Engine*, no GameObject inheritance, no Renderer2D.
class Manager {
  int sideSize = 17;
  Grid2D<Square> grid;
  float enemyTickSize = 0.5f;
  float timeTimeRemaining = 0.5f;
  bool showHiddenObjects = true;

public:
  Manager() = default;

  void Start();
  void OnGui();
  void OnDraw();
  void Update(float deltaTime);

  // helper functions
  glm::ivec2 screenSpaceToGridIndex(ImVec2& pos);
  void Reset();

  // game logic
  void EnemyTick();
  void ShadowCast();
};

#endif  // MOBAGEN_MANAGERHIDEANDSEEK_H
