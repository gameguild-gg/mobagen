#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <imgui.h>
#include "Engine.h"
#include "Transform.h"

class Engine;

// todo: create a game list of game components of a game object
class GameObject {
public:
    Transform transform;
    Engine* engine;
public:
    explicit GameObject(Engine* pEngine);

    virtual void Start(){};
    virtual void OnGui(ImGuiContext *context) {};
    virtual void OnDraw(SDL_Renderer* renderer) {};
    virtual void Update(float deltaTime) {};
};
#endif