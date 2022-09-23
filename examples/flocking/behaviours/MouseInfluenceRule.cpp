#include "MouseInfluenceRule.h"
#include "../gameobjects/Boid.h"
#include "../gameobjects/World.h"

Vector2 MouseInfluenceRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    ImGui::SetCurrentContext(world->engine->imGuiContext);
//    ImGuiIO& io = ImGui::GetIO();
//    if (ImGui::IsMousePosValid() && io.MouseDown[0]) {
//        Vector2 mousePos(io.MousePos.x, io.MousePos.y); // todo: use this
//        Vector2 displacement = Vector2::zero(); // todo: change this
//        float distance = 0; // todo: change this
//
//        //The force is inversely proportional to distance
//        Vector2 force = Vector2::zero(); // todo: change this
//
//        if (isRepulsive)
//            force *= -1.f;
//
//        return force;
//    }
//    else
//        return Vector2::zero();
    return Vector2::zero();
}

bool MouseInfluenceRule::drawImguiRuleExtra() {
    bool valueHasChanged = false;

    if (ImGui::RadioButton("Attractive", !isRepulsive)) {
        isRepulsive = false;
        valueHasChanged = true;
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Repulsive", isRepulsive)) {
        isRepulsive = true;
        valueHasChanged = true;
    }

    return valueHasChanged;
}