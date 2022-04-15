#include "SeparationRule.h"
#include "../Boid.h"

Vector2 SeparationRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) {

    //Try to avoid boids too close
    Vector2 separatingForce;

    float desiredDistance = desiredMinimalDistance;

    if (neighbordhood.size() > 0) {
        Vector2 position = boid->transform.position;
        int countCloseFlockmates = 0;

        for (auto const& flockmate : neighbordhood)
        {
            float distance = vector2::getDistance(position, flockmate->getPosition());

            if (distance < desiredDistance && distance > 0)
            {
                sf::Vector2f opposedDirection = vector2::normalized(position - flockmate->getPosition());

                //Each neighbor has an influence proportional to its distance
                separatingForce += opposedDirection / distance;
                countCloseFlockmates++;
            }
        }

        if (countCloseFlockmates > 0) {
            //We doesn't divide by neighboor counts, but too clost neighbor counts.
            separatingForce /= static_cast<float>(countCloseFlockmates);
        }
    }

    separatingForce = vector2::normalized(separatingForce);

    return separatingForce;
}

bool SeparationRule::drawImguiRuleExtra()
{
    bool valusHasChanged = false;

    if (ImGui::DragFloat("Desired Separation", &desiredMinimalDistance, 0.05f))
    {
        valusHasChanged = true;
    }

    return valusHasChanged;
}
