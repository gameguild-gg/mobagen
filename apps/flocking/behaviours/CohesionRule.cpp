#include "CohesionRule.h"

#include <glm/glm.hpp>

glm::vec2 CohesionRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 cohesionForce(0.f);

  // glm::length(vec) returns the length of a vector,
  // glm::normalize(vec) returns the normalized vector (length 1) in the same direction as vec.

  // begin solution

  glm::vec2 positionTotal = glm::vec2(0, 0);
  glm::vec2 forceNeeded = glm::vec2(0, 0);
  float numOfNeighbours = 0.0f;

  for (const BoidView& boidInRange : neighborhood)
  {
    positionTotal += boidInRange.position;
    numOfNeighbours++;
  }

  if (numOfNeighbours != 0)
  {
    glm::vec2 centerMass = positionTotal / numOfNeighbours;
    forceNeeded = centerMass - boid.position;
    forceNeeded = glm::normalize(forceNeeded);
  }

  // end solution
  cohesionForce = forceNeeded;
  return cohesionForce;
}