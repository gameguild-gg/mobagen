#include "CohesionRule.h"

#include <glm/glm.hpp>

glm::vec2 CohesionRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 cohesionForce(0.f);

  // glm::length(vec) returns the length of a vector,
  // glm::normalize(vec) returns the normalized vector (length 1) in the same direction as vec.

  // begin solution

  glm::vec2 positionTotal;
  glm::vec2 centerMass;
  glm::vec2 forceNeeded;

  for (const BoidView& boid : neighborhood)
  {
    positionTotal += boid.position;
  }

  //centerMass = positionTotal / neighborhood.size();
  forceNeeded = centerMass - boid.position;
  forceNeeded = glm::normalize(forceNeeded);

  // end solution
  cohesionForce = forceNeeded;
  return cohesionForce;
}
