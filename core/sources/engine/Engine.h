#ifndef ENGINE_H
#define ENGINE_H

#include "EngineForwards.h"
#include <imgui.h>
#include "../Window.h"
#include "../scene/SceneForwards.h"
#include "EngineSettings.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

class Renderer2D;
namespace Rml { class Context; }

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

  // Source of truth lives in `settings.clearColor`; kept here as a member
  // so the render pass descriptor can read it without chasing the settings
  // struct every frame.
  float clearColor[4] = {0.f, 0.f, 0.f, 1.f};

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

  // Hook called at the end of every Tick() so examples can update per-frame
  // diagnostics (e.g. RmlUi text showing current window/logical/pixel sizes).
  std::function<void()> onTick;
};
#endif
