#define SDL_MAIN_HANDLED true
#include <Engine.h>
#include "gameobjects/World.h"

// Reference https://github.com/boardtobits/flocking-algorithm

// Main code
int main(int, char**) {
    SDL_Log("Creating Engine");
    auto engine = new Engine();
    SDL_Log("Engine Created");

    SDL_Log("Creating World Object");
    new World(engine);
    SDL_Log("World Created");

    SDL_Log("Starting Engine");
    if(engine->Start("Flocking")) {
        SDL_Log("Engine Started");

        SDL_Log("Running Engine");
        engine->Run();
        SDL_Log("Engine Stopped");
    }

    SDL_Log("Exiting Engine");
    engine->Exit();
    SDL_Log("Engine Exited");
    return 0;
}

