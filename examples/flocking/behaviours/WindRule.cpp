#include "WindRule.h"
#include <imgui.h>

sf::Vector2f WindRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{
    return utils::vector2::normalized(utils::vector2::getVector2FromRadian(windAngle));
}

bool WindRule::drawImguiRuleExtra()
{
    bool valusHasChanged = false;

    if (ImGui::SliderAngle("Wind Direction", &windAngle, 0))
    {
        valusHasChanged = true;
    }

    return valusHasChanged;
}