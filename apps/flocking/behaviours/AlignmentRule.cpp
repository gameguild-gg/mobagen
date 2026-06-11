#include "AlignmentRule.h"
#include <glm/glm.hpp>

glm::vec2 AlignmentRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 averageVelocity(0.f);

  float len = glm::length(averageVelocity);
  return len > 0.0001f ? averageVelocity / len : glm::vec2(0.f);
}
