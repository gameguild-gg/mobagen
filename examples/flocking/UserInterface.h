#pragma once

#include <GameObject.h>
#include <imgui.h>
#include <Engine.h>

class UserInterface: GameObject {
    void OnGui(ImGuiContext *context) override;

public:
    explicit UserInterface(Engine *pEngine);
};