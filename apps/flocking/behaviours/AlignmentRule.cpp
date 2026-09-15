#include "AlignmentRule.h"
#include <glm/glm.hpp>

glm::vec2 AlignmentRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid)
{
  glm::vec2 totalVelocity(0.0f);
  for (const BoidView& neighbor : neighborhood)
  {
    totalVelocity += neighbor.velocity;
  }

  totalVelocity += boid.velocity;

  size_t n = neighborhood.size() + 1;

  glm::vec2 averageVelocity = totalVelocity / static_cast<float>(n);

  return averageVelocity;
}