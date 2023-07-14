#ifndef SCENARIO_MANAGER_H
#define SCENARIO_MANAGER_H

#include "math/ColorT.h"
#include "scene/GameObject.h"
#include "GeneratorBase.h"
#include <SDL.h>

// ref https://e2eml.school/transformers.html

class Manager : public GameObject {
private:
  float accumulatedTime = 0;
  int sideSize = 512;
  SDL_Texture* texture;
  bool isSimulating = false;

  std::vector<ScenarioGeneratorBase*> generators;
  int generatorId = 0;

  void step();

public:
  ~Manager();
  explicit Manager(Engine* engine, int size);

  void Start() override;
  void OnGui(ImGuiContext* context) override;
  void OnDraw(SDL_Renderer* renderer) override;
  void Update(float deltaTime) override;

  void Clear();
  int GetSize() const;

  void SetPixels(std::vector<Color32>& pixels);
};

#endif
