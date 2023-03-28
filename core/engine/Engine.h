#ifndef ENGINE_H
#define ENGINE_H

#include "EngineForwards.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer.h"
#include "../Window.h"
#include "../scene/SceneForwards.h"
#include <cstdio>
#include "SDL.h"
#include <vector>
#include <unordered_set>
#include <chrono>

class Engine {
private:
  std::chrono::high_resolution_clock::time_point lastFrameTime;
  float deltaTime;
  double targetFPS = 60;
  int64_t accumulatedTime = 0;

public:
  Window* window;

  // todo: move this to a scene manager and make this private
  std::unordered_set<GameObject*> gameObjects;
  Vector2 getInputArrow() const;

private:
  bool done = false;

  ImVec4 clear_color = ImVec4(0, 0, 0, 1);

  // todo: move this to input class
  void processInput();
  Vector2 arrowInput;

  // todo: better ordering
  std::vector<GameObject*> toDestroy;

public:
  Engine();
  ~Engine();
  bool Start(std::string title);
  void Run();
  void Tick();
  void Exit();

  //  template <class T> std::unordered_set<T> FindObjectsOfType();

  void Destroy(GameObject* go);
};
#endif