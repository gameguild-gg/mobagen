#include "AlignmentRule.h"
#include "../gameobjects/Boid.h"

Vector2 AlignmentRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    //Try to match the heading of neighbors = Average velocity
    Vector2 averageVelocity;

    if (!neighborhood.empty()) {
        for (auto flockmate : neighborhood)
            averageVelocity += flockmate->velocity;
        averageVelocity /= static_cast<float>(neighborhood.size());
    }

    return Vector2::normalized(averageVelocity);
}