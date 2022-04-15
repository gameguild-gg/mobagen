#include "WindRule.h"
#include <imgui.h>
#include "../Boid.h"

Vector2 WindRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) {
    return Vector2::normalized(Vector2::getVector2FromRadian(windAngle));
}

bool WindRule::drawImguiRuleExtra() {
    bool valueHasChanged = false;

    if (ImGui::SliderAngle("Wind Direction", &windAngle, 0)) {
        valueHasChanged = true;
    }

    return valueHasChanged;
}