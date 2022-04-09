#pragma once

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_sdlrenderer.h"
#include "Window.h"
#include "GameObject.h"
#include <cstdio>
#include <SDL.h>
#include <vector>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

class Engine{
public:
    // todo: wrap this as a get instance and protect against writes
    static inline Engine *instance = nullptr;
    Window *window;

private:
    bool done = false;
    std::vector<GameObject> gameObjects;

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // todo: better ordering
public:
#ifdef EMSCRIPTEN
    static void loop();
#endif
    Engine();
    ~Engine();
    int Start();
    void Run();
    void Tick();
    void Exit();
};