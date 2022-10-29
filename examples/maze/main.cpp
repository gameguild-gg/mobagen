#define SDL_MAIN_HANDLED true
#include <Engine.h>
#include "World.h"

int main()
{
    SDL_Log("Creating Engine");
    auto engine = new Engine();
    SDL_Log("Engine Created");

    SDL_Log("Creating World Object");
    new World(engine, 21);
    SDL_Log("World Created");

    SDL_Log("Starting Engine");
    if(engine->Start("Maze")) {
        SDL_Log("Engine Started");
        SDL_Log("Running Engine");
        engine->Run();
        SDL_Log("Engine Stopped");
    }

    SDL_Log("Exiting Engine");
    engine->Exit();
    delete engine;
    SDL_Log("Engine Exited");
    return 0;
}

