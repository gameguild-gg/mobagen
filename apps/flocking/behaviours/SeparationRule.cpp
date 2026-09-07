#include "SeparationRule.h"
#include "imgui.h"
#include <glm/glm.hpp>

glm::vec2 SeparationRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 separatingForce(0.f);

  // the header have the desiredMinimalDistance member variable, which is the distance that the boids should try to maintain from each other.
  // glm::length(vec) returns the length of a vector,
  // glm::normalize(vec) returns the normalized vector (length 1) in the same direction as vec.
  // multiply by (desiredMinimalDistance / distance) is the proportionality factor that makes the force stronger when the boids are closer together, and weaker when they are farther apart.

  // begin solution

  glm::vec2 totalForces = glm::vec2(0, 0);
  glm::vec2 vector = glm::vec2(0, 0);
  glm::vec2 hat = glm::vec2(0, 0);
  double magnitude = 0;
  int numOfNeighbours = 0;

  for (const BoidView& boidInRange : neighborhood)
  {
    vector = boid.position - boidInRange.position;
    magnitude = sqrt(vector.x * vector.x + vector.y * vector.y);
    hat = normalize(vector);
    if (magnitude > 0.0f && magnitude <= desiredMinimalDistance)
    {
      totalForces += hat * (desiredMinimalDistance / float(magnitude));
      numOfNeighbours++;
    }
  }

  if (numOfNeighbours != 0)
  {
    if (sqrt(totalForces.x * totalForces.x + totalForces.y * totalForces.y) > weight)
      totalForces = normalize(totalForces) * weight;

    separatingForce = totalForces;
  }

  // end solution

  return separatingForce;
}

bool SeparationRule::drawImguiRuleExtra() {
  bool valueHasChanged = false;
  if (ImGui::DragFloat("Desired Separation", &desiredMinimalDistance, 0.05f)) {
    valueHasChanged = true;
  }
  return valueHasChanged;
}
