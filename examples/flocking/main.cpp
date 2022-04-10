#include <Engine.h>
#include "UserInterface.h"
#include "Ship.h"

// Reference code https://github.com/boardtobits/flocking-algorithm

// Main code
int main(int, char**) {
    auto engine = new Engine();
    new UserInterface(engine);
    new Ship(engine);
    if(engine->Start())
        engine->Run();
    delete(engine);
    return 0;
}

