#include "CohesionRule.h"
#include <glm/glm.hpp>

glm::vec2 CohesionRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 cohesionForce(0.f);

  // glm::length(vec) returns the length of a vector,
  // glm::normalize(vec) returns the normalized vector (length 1) in the same direction as vec.

  // begin solution
  glm::vec2 centerMass(0.f);
  float count = 0;
  for (BoidView neighbor : neighborhood) 
  {
    centerMass += neighbor.position;
    count++;
  }
  if (count>0) {
    centerMass = centerMass / count;
    cohesionForce = centerMass - boid.position;
    cohesionForce = glm::normalize(cohesionForce);
  }
  // end solution

  return cohesionForce;
}
