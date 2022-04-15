#include "AlignmentRule.h"
#include "../Boid.h"

Vector2 AlignmentRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) {
    //Try to match the heading of neighbors = Average velocity
    Vector2 averageVelocity;

    if (!neighbordhood.empty()) {
        for (auto flockmate : neighbordhood)
            averageVelocity += flockmate->velocity;
        averageVelocity /= static_cast<float>(neighbordhood.size());
    }

    return Vector2::normalized(averageVelocity);
}