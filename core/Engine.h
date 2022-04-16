#ifndef ENGINE_H
#define ENGINE_H

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

class GameObject;

class Engine{
public:
    Window *window;
    ImGuiContext *imGuiContext;

    // todo: move this to a scene manager and make this private
    std::vector<GameObject*> gameObjects;
    Vector2 getInputArrow() const;

private:
    bool done = false;

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0, 0, 0, 1);


    // todo: move this to input class
    void processInput();
    Vector2 arrowInput;

    // todo: better ordering
public:
    Engine();
    ~Engine();
    int Start(std::string title);
    void Run();
    void Tick();
    void Exit();


    template<class T>
    std::vector<T> FindObjectsOfType();

    void Destroy(GameObject* go);
};
#endif