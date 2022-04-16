#include "CohesionRule.h"
#include "../gameobjects/Boid.h"

Vector2 CohesionRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    Vector2 cohesionForce;

    if (!neighborhood.empty()) {
        Vector2 centerOfMass;

        for (auto const& flockmate : neighborhood)
            centerOfMass += flockmate->transform.position;
        centerOfMass /= static_cast<float>(neighborhood.size());

        //Get direction toward center of mass
        Vector2 towardCenter = centerOfMass - boid->transform.position;

        cohesionForce = Vector2::normalized(towardCenter);
    }

    return cohesionForce;
}