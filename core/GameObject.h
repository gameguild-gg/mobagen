#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <imgui.h>
#include "Engine.h"

class Engine;

class GameObject {
public:
    Engine* engine;
public:
    explicit GameObject(Engine* engine);
    virtual void OnGui(ImGuiContext *context) {};
    virtual void OnDraw(SDL_Renderer* renderer) {};
};
#endif