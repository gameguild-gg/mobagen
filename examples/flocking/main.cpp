#include <Engine.h>
#include "UserInterface.h"
#include "Ship.h"

// Reference code https://github.com/boardtobits/flocking-algorithm

// Main code
int main(int, char**) {
    SDL_Log("Creating Engine");
    auto engine = new Engine();
    SDL_Log("Engine Created");

    SDL_Log("Creating Game Objects");
    new UserInterface(engine);
    new Ship(engine);
    SDL_Log("Game Objects Created");

    SDL_Log("Starting Engine");
    if(engine->Start("Flocking")) {
        SDL_Log("Engine Started");
        engine->Run();
    }

    SDL_Log("Exiting Engine");
    engine->Exit();
    SDL_Log("Engine Exited");
}

