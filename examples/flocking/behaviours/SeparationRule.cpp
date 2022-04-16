#include "SeparationRule.h"
#include "../Boid.h"
#include "../World.h"

Vector2 SeparationRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    //Try to avoid boids too close
    Vector2 separatingForce;

    float desiredDistance = desiredMinimalDistance;

    if (!neighborhood.empty()) {
        Vector2 position = boid->transform.position;
        int countCloseFlockmates = 0;

        for (auto const& flockmate : neighborhood) {
            float distance = Vector2::getDistance(position, flockmate->getPosition());

            if (distance < desiredDistance && distance > 0) {
                Vector2 opposedDirection = Vector2::normalized(position - flockmate->getPosition());

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

    separatingForce = Vector2::normalized(separatingForce);

    return separatingForce;
}

bool SeparationRule::drawImguiRuleExtra() {
    ImGui::SetCurrentContext(world->engine->imGuiContext);
    bool valusHasChanged = false;

    if (ImGui::DragFloat("Desired Separation", &desiredMinimalDistance, 0.05f)) {
        valusHasChanged = true;
    }

    return valusHasChanged;
}
