#include "MouseInfluenceRule.h"
#include "../Boid.h"
#include "../World.h"

Vector2 MouseInfluenceRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    ImGui::SetCurrentContext(world->engine->imGuiContext);
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMousePosValid() && io.MouseDown[0]) {
        Vector2 mousePos(io.MousePos.x, io.MousePos.y);
        Vector2 displacement = mousePos - boid->transform.position;
        float distance = Vector2::getMagnitude(displacement);

        //The force is inversely proportional to distance
        Vector2 force = displacement / exp(distance / 100.f);

        if (isRepulsive)
            force *= -1.f;

        return force;
    }
    else
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