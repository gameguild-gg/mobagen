#include "WindRule.h"
#include <imgui.h>
#include "../Boid.h"
#include "../World.h"

Vector2 WindRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    return Vector2::normalized(Vector2::getVector2FromRadian(windAngle));
}

bool WindRule::drawImguiRuleExtra() {
    ImGui::SetCurrentContext(world->engine->imGuiContext);
    bool valueHasChanged = false;

    if (ImGui::SliderAngle("Wind Direction", &windAngle, 0)) {
        valueHasChanged = true;
    }

    return valueHasChanged;
}