#include "CohesionRule.h"

Vector2 CohesionRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) {
    Vector2 cohesionForce;

    if (neighbordhood.size() > 0) {
        Vector2 centerOfMass;

        for (auto const& flockmate : neighbordhood)
            centerOfMass += flockmate->transform.position;
        centerOfMass /= static_cast<float>(neighbordhood.size());

        //Get direction toward center of mass
        Vector2 towardCenter = centerOfMass - boid->transform.position;

        cohesionForce = Vector2::normalized(towardCenter);
    }

    return cohesionForce;
}