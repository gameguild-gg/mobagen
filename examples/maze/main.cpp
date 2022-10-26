#include <Engine.h>
#include "World.h"

// Main code
int main(int, char**) {
  // test all node cases
  for(int north=0; north<2; north++) {
    for(int east=0; east<2; east++) {
      for(int south=0; south<2; south++) {
        for(int west=0; west<2; west++) {
          Node n = {north==1, east==1, south==1, west==1};
          std::cout << "case: " <<  north << east << south << west << std::endl;
          std::cout << n.GetNorth() << n.GetEast() << n.GetSouth() << n.GetWest() << std::endl;
          n.SetNorth(true); n.SetEast(true); n.SetSouth(true); n.SetWest(true);
          std::cout << n.GetNorth() << n.GetEast() << n.GetSouth() << n.GetWest() << std::endl;
          n.SetNorth(false); n.SetEast(false); n.SetSouth(false); n.SetWest(false);
          std::cout << n.GetNorth() << n.GetEast() << n.GetSouth() << n.GetWest() << std::endl;
        }
      }
    }
  }

    SDL_Log("Creating Engine");
    auto engine = new Engine();
    SDL_Log("Engine Created");

    SDL_Log("Creating World Object");
    new World(engine, 21);
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

