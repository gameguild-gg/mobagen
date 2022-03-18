#include <Engine.h>

// Main code
int main(int, char**)
{
    auto engine = new Engine();
    if(engine->Start())
        engine->Run();
    engine->Exit();
    return 0;
}
