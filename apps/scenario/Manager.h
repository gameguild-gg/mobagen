#ifndef SCENARIO_MANAGER_H
#define SCENARIO_MANAGER_H

#include "imgui.h"
#include "math/ColorT.h"
#include "GeneratorBase.h"

#include <vector>

class Manager {
private:
  float accumulatedTime = 0;
  int sideSize = 128;
  bool isSimulating = false;

  std::vector<ScenarioGeneratorBase*> generators;
  int generatorId = 0;

  std::vector<Color32> pixels;

  void step();

public:
  Manager();
  ~Manager();

  void Start();
  void OnGui();
  void OnDraw();
  void Update(float deltaTime);

  void Clear();
  int GetSize() const;

  void SetPixels(std::vector<Color32>& px);
};

#endif  // SCENARIO_MANAGER_H
