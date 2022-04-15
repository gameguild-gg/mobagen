#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <GameObject.h>
#include <imgui.h>
#include <Engine.h>

class UserInterface: GameObject {
public:
    explicit UserInterface(Engine *pEngine);

    void OnGui(ImGuiContext *context) override;
};
#endif