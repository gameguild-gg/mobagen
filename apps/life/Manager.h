#ifndef LIFE_MANAGER_H
#define LIFE_MANAGER_H

#include "imgui.h"
#include "RuleBase.h"
#include "World.h"

#include <glm/glm.hpp>
#include <vector>

class Manager {
private:
  int sideSize = 13;
  World world;
  bool isSimulating = false;
  float accumulatedTime = 0;
  float timeBetweenSteps = 0.2f;
  void step();
  void clear();
  std::vector<RuleBase*> rules;
  int ruleId = 0;
  glm::ivec2 mousePositionToIndex(ImVec2& pos);

public:
  Manager();
  ~Manager();

  void Start();
  void OnGui();
  void OnDraw();
  void Update(float deltaTime);
};

#endif  // LIFE_MANAGER_H
