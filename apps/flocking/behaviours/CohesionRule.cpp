#include "CohesionRule.h"
#include <glm/glm.hpp>

glm::vec2 CohesionRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid)
{
  glm::vec2 cohesionForce(0.0f);

  // glm::length(vec) returns the length of a vector,
  // glm::normalize(vec) returns the normalized vector (length 1) in the same direction as vec.

  // begin solution

  size_t n = neighborhood.size();
  if (n == 0)
  {
    return cohesionForce;
  }

  glm::vec2 centerOfMass(0.0f);
  for (const BoidView& neighbor : neighborhood)
  {
    centerOfMass += neighbor.position;
  }

  centerOfMass /= static_cast<float>(n);

  glm::vec2 direction = centerOfMass - boid.position;

  if (glm::length(direction) > 0.0f)
  {
    cohesionForce = glm::normalize(direction);
  }

  // end solution

  return cohesionForce;
}