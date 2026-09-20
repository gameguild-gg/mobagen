#include "CohesionRule.h"
#include "imgui.h"
#include <glm/glm.hpp>

glm::vec2 CohesionRule::computeForce(const std::vector<BoidView>& boids, int selfIndex) {
  glm::vec2 cohesionForce(0.f);

  // glm::length(vec) returns the length of a vector,
  // glm::normalize(vec) returns the normalized vector (length 1) in the same direction as vec.
  // boids contains every boid, including this one (boids[selfIndex]);
  // use radius to scale the force so it is stronger when the boid is far from the center of mass
  // the force magnitude should be between 0 and 1. 1 is when the CM is at the edge of the radius, and 0 is when it is at the center of mass. 
  // Bonus: use spatial hashing to avoid O(n^2) complexity. Implement that on World.

  // begin solution

  // end solution

  return cohesionForce;
}

bool CohesionRule::drawImguiRuleExtra() {
  bool valueHasChanged = false;
  if (ImGui::DragFloat("Detection Radius", &radius, 1.f, 0.f, 500.f)) {
    valueHasChanged = true;
  }
  return valueHasChanged;
}

void CohesionRule::drawRadius(const BoidView& boid, ImDrawList* dl) const {
  dl->AddCircle({boid.position.x, boid.position.y}, radius,
                IM_COL32(static_cast<int>(debugColor.r * 255), static_cast<int>(debugColor.g * 255), static_cast<int>(debugColor.b * 255), 64), 32);
}
