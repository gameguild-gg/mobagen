#include "MouseInfluenceRule.h"

sf::Vector2f MouseInfluenceRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{

    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMousePosValid() && io.MouseDown[0])
    {
        sf::Vector2f mousePos(io.MousePos.x, io.MousePos.y);
        sf::Vector2f displacement = mousePos - boid->getPosition();
        float distance = utils::vector2::getMagnitude(displacement);

        //The force is inversely proportional to distance

        sf::Vector2f force = displacement / exp(distance / 100.f);

        if (isRepulsive)
        {
            force *= -1.f;
        }

        return force;
    }
    else
    {
        return sf::Vector2f();
    }

}

bool MouseInfluenceRule::drawImguiRuleExtra()
{

    bool valusHasChanged = false;

    if (ImGui::RadioButton("Attractive", !isRepulsive))
    {
        isRepulsive = false;
        valusHasChanged = true;
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Repulsive", isRepulsive))
    {
        isRepulsive = true;
        valusHasChanged = true;
    }

    return valusHasChanged;
}