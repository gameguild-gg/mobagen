#include "CohesionRule.h"

sf::Vector2f CohesionRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{

    sf::Vector2f cohesionForce;

    if (neighbordhood.size() > 0)
    {

        sf::Vector2f centerOfMass;

        for (auto const& flockmate : neighbordhood)
        {
            centerOfMass += flockmate->getPosition();
        }

        centerOfMass /= static_cast<float>(neighbordhood.size());

        //Get direction toward center of mass
        sf::Vector2f towardCenter = centerOfMass - boid->getPosition();

        cohesionForce = utils::vector2::normalized(towardCenter);
    }

    return cohesionForce;
}