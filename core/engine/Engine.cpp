#include "Engine.h"
#include "SDL.h"
#include "../Polygon.h"
#include "../scene/GameObject.h"
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

using namespace std::chrono_literals;

Engine::Engine() { window = nullptr; }

Engine::~Engine() {
  if (window) {
    // Cleanup
    ImGui_ImplSDLRenderer_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    delete (window);
  }

  for (auto go : gameObjects) delete (go);  // clear all remaining game objects
  gameObjects.clear();
  SDL_Log("Game Objects Cleared");
}
// https://gafferongames.com/post/fix_your_timestep/
void Engine::Run() {
  // Main loop
  for (;;) {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count();
    deltaTime = duration / 1000000.0f;
    accumulatedTime += duration;
    lastFrameTime = currentTime;

    int64_t targetTimeToSleep = 1000000 / targetFPS;
    for (; accumulatedTime >= targetTimeToSleep; accumulatedTime -= targetTimeToSleep) {
      Tick();
      if (done) return;
    }
    if (targetTimeToSleep >= accumulatedTime)
      targetTimeToSleep -= accumulatedTime;
    else
      targetTimeToSleep = 0;
#ifdef __EMSCRIPTEN__
    emscripten_sleep(targetTimeToSleep / 1000);
#else
    // std::this_thread::sleep_for(targetTimeToSleep * 1ms/1000);
    SDL_Delay(targetTimeToSleep / 1000);
#endif
  }
}

bool Engine::Start(std::string title) {
  SDL_Log("Initializing Window");
  window = new Window(title);
  if (window != nullptr)
    SDL_Log("Window Initialized");
  else
    exit(0);

  // start all gameobjects
  for (auto go : gameObjects) go->Start();

  lastFrameTime = std::chrono::high_resolution_clock::now();
  SDL_Delay(1000 / targetFPS);
  deltaTime = 0;

  return true;
}

void Engine::Tick() {
  // Start the Dear ImGui frame
  ImGui_ImplSDLRenderer_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  window->Update();

  SDL_SetRenderDrawColor(window->sdlRenderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255),
                         (Uint8)(clear_color.w * 255));
  SDL_RenderClear(window->sdlRenderer);

  // inputs processing
  processInput();

  // update
  for (auto go : gameObjects) go->Update(deltaTime);

  // iterate over all game objects ui
  auto gos = gameObjects;                               // clone to prevent out of bounds access
  for (auto go : gos) go->OnGui(window->imGuiContext);  // todo: find a better way to pass imgui context

  // Rendering
  ImGui::Render();

  // Draw
  for (auto go : gameObjects) go->OnDraw(window->sdlRenderer);

  // destroy objects marked to death
  if (!toDestroy.empty()) {
    for (auto go : toDestroy) {
      gameObjects.erase(go);
      delete (go);
    }
    toDestroy.clear();
  }

  ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
  SDL_RenderPresent(window->sdlRenderer);
}

void Engine::Exit() {
  SDL_Log("Exit called");
  done = true;
}

void Engine::processInput() {
  // Poll and handle events (inputs, window resize, etc.)
  // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
  // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
  // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
  // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.

  // todo: move this to Input
  static bool up = false, down = false, left = false, right = false;

  // clean the state
  arrowInput = Vector2();

  // process events
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT) done = true;
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window->sdlWindow))
      done = true;

    // todo: improve the key strokes tracking
    switch (event.type) {
      case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
          case SDLK_LEFT:
            left = true;
            break;
          case SDLK_RIGHT:
            right = true;
            break;
          case SDLK_UP:
            up = true;
            break;
          case SDLK_DOWN:
            down = true;
            break;
          default:
            break;
        }
        break;
      case SDL_KEYUP:
        switch (event.key.keysym.sym) {
          case SDLK_LEFT:
            left = false;
            break;
          case SDLK_RIGHT:
            right = false;
            break;
          case SDLK_UP:
            up = false;
            break;
          case SDLK_DOWN:
            down = false;
            break;
          default:
            break;
        }
        break;
    }
  }
  if (up) arrowInput += Vector2::up();
  if (down) arrowInput += Vector2::down();
  if (left) arrowInput += Vector2::left();
  if (right) arrowInput += Vector2::right();
}

Vector2 Engine::getInputArrow() const { return arrowInput; }

// todo: optimize this
template <class T> std::unordered_set<T> Engine::FindObjectsOfType() {
  std::unordered_set<T> ret;
  for (GameObject* go : gameObjects)
    if (T elem = dynamic_cast<T>(&go))  // todo: check this
      ret.insert(elem);

  return ret;
}

// todo: optimize this
void Engine::Destroy(GameObject* go) { toDestroy.push_back(go); }
