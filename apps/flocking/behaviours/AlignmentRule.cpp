#include "AlignmentRule.h"
#include <glm/glm.hpp>

glm::vec2 AlignmentRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 averageVelocity(0.f);
  // glm::vec2 can be divided by a float, which will divide each component of the vector by that float.

  // begin solution
  glm::vec2 velocityTotal = glm::vec2(0, 0);
  int numOfNeighbours = 0;

  for (const BoidView& boidInRange : neighborhood)
  {
    velocityTotal += boidInRange.velocity;
    numOfNeighbours++;
  }

  if (numOfNeighbours != 0)
    averageVelocity = glm::vec2(velocityTotal.x / numOfNeighbours, velocityTotal.y / numOfNeighbours);

  return averageVelocity;
  // end solution
}
