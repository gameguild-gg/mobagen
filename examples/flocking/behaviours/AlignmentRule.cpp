#include "AlignmentRule.h"

sf::Vector2f AlignmentRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{
    //Try to match the heading of neighbors = Average velocity

    sf::Vector2f averageVelocity;

    if (neighbordhood.size() > 0)
    {

        for (auto const& flockmate : neighbordhood)
        {
            averageVelocity += (flockmate)->getVelocity();
        }

        averageVelocity /= static_cast<float>(neighbordhood.size());
    }

    return utils::vector2::normalized(averageVelocity);
}