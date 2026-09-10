#include "AlignmentRule.h"
#include <glm/glm.hpp>

glm::vec2 AlignmentRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 averageVelocity(0.f);
  // glm::vec2 can be divided by a float, which will divide each component of the vector by that float.

  // begin solution
  
  averageVelocity += boid.velocity;
  float count = 1;
  for (BoidView neighbor : neighborhood) 
  {
    averageVelocity += neighbor.velocity;
    count++;
  }

  averageVelocity = averageVelocity / count;

  return averageVelocity;
  // end solution
}
