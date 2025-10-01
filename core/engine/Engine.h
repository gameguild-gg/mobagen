#ifndef ENGINE_H
#define ENGINE_H

#include "EngineForwards.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "../Window.h"
#include "../scene/SceneForwards.h"
#include <cstdio>
#include "SDL.h"
#include "EngineSettings.h"
#include <vector>
#include <unordered_set>
#include <chrono>
#include <cstdint>

class Engine {
private:
  std::chrono::high_resolution_clock::time_point lastFrameTime;
  float deltaTime;
  double targetFPS = 60;
  int64_t accumulatedTime = 0;
  EngineSettings settings;
  inline static Engine* instance = nullptr;

public:
  static Engine* GetInstance() {
    if (instance == nullptr) instance = new Engine();
    return instance;
  }
  Window* window;

  // todo: move this to a scene manager and make this private
  std::unordered_set<GameObject*> gameObjects;
  std::unordered_set<GameObject*> gameObjectsToBeStarted;
  std::unordered_set<ScriptableObject*> scriptableObjects;
  Vector2f getInputArrow() const;

private:
  bool done = false;

  ImVec4 clear_color = ImVec4(0, 0, 0, 1);

  // todo: move this to input class
  void processInput();
  Vector2f arrowInput;

  // todo: better ordering
  std::vector<GameObject*> toDestroy;

public:
  explicit Engine(EngineSettings settings = EngineSettings());

  ~Engine();
  bool Start(std::string title);
  void Run();
  void Tick();
  void Exit();

  //  template <class T> std::unordered_set<T> FindObjectsOfType();

  void Destroy(GameObject* go);

  void AddScriptableObject(ScriptableObject* pObject) { scriptableObjects.insert(pObject); };

  bool IsHeadless() const { return settings.headless; }
};
#endif