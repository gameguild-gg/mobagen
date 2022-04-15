#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <imgui.h>
#include "Engine.h"
#include "Transform.h"

class Engine;

// todo: create a game list of game components of a game object
class GameObject {
public:
    Engine* engine;
    Transform transform;
public:
    explicit GameObject(Engine* engine);
    virtual void OnGui(ImGuiContext *context) {};
    virtual void OnDraw(SDL_Renderer* renderer) {};
    virtual void Update(float deltaTime) {};
};
#endif