#pragma once

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_sdlrenderer.h"
#include "Window.h"
#include <cstdio>
#include <SDL.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

class Engine{
public:

    bool done = false;

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    Window *window;

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