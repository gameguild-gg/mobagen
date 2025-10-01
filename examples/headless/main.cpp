#define SDL_MAIN_HANDLED true
#include "engine/Engine.h"
#include "scene/GameObject.h"
#include "SDL.h"
#include <iostream>

// Simple test object that runs for a limited time in headless mode
class HeadlessTestObject : public GameObject {
private:
  float totalTime = 0.0f;
  float maxRunTime = 5.0f;  // Run for 5 seconds
  int frameCount = 0;

public:
  HeadlessTestObject(Engine* pEngine) : GameObject(pEngine) {}

  void Update(float deltaTime) override {
    totalTime += deltaTime;
    frameCount++;
    
    // Log progress every second
    if (frameCount % 60 == 0) {  // Assuming 60 FPS
      SDL_Log("Headless simulation running: %.2f seconds, frame %d", totalTime, frameCount);
    }
    
    // Exit after maxRunTime seconds
    if (totalTime >= maxRunTime) {
      SDL_Log("Headless simulation completed after %.2f seconds (%d frames)", totalTime, frameCount);
      engine->Exit();
    }
  }

  void OnDraw(SDL_Renderer* renderer) override {
    // This should never be called in headless mode
    SDL_Log("Warning: OnDraw called in headless mode!");
  }

  void OnGui(ImGuiContext* context) override {
    // This should never be called in headless mode
    SDL_Log("Warning: OnGui called in headless mode!");
  }
};

// Main code
int main(int, char**) {
  SDL_Log("Creating Headless Engine");
  
  // Create engine settings with headless mode enabled
  EngineSettings settings;
  settings.headless = true;
  settings.debug = true;
  
  auto engine = new Engine(settings);
  SDL_Log("Headless Engine Created");

  SDL_Log("Creating Test Object");
  new HeadlessTestObject(engine);
  SDL_Log("Test Object Created");

  SDL_Log("Starting Headless Engine");
  if (engine->Start("Headless Test")) {
    SDL_Log("Headless Engine Started");

    SDL_Log("Running Headless Engine");
    engine->Run();
    SDL_Log("Headless Engine Stopped");
  }

  SDL_Log("Exiting Headless Engine");
  engine->Exit();
  delete engine;
  SDL_Log("Headless Engine Exited");
  
  std::cout << "Headless simulation completed successfully!" << std::endl;
  return 0;
}