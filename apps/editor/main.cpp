#define SDL_MAIN_HANDLED true
#include "engine/Engine.h"
#include "TopBar.h"
#include <vector>

// Main code
int main(int, char**) {
  SDL_Log("Creating Engine");
  auto engine = new Engine();
  SDL_Log("Engine Created");
  new TopBar();

  SDL_Log("Starting Engine");
  if (engine->Start("Editor")) {
    SDL_Log("Engine Started");
    engine->Run();
  }

  SDL_Log("Exiting Engine");
  engine->Exit();
  SDL_Log("Engine Exited");
  return 0;
}