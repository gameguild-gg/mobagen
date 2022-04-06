#include <Engine.h>

// Reference code https://github.com/boardtobits/flocking-algorithm

// Main code
int main(int, char**) {
    auto engine = new Engine();
    if(engine->Start())
        engine->Run();
    delete(engine);
    return 0;
}

