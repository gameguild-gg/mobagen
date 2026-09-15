#include "SeparationRule.h"
#include "imgui.h"

#include <ostream>
#include <glm/glm.hpp>

glm::vec2 SeparationRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid)
{
  glm::vec2 separatingForce(0.f);

  // begin solution

  for (const BoidView& neighbor : neighborhood)
  {
    glm::vec2 offset = boid.position - neighbor.position;
    float distance = glm::length(offset);

    if (distance > 0.0001f && distance <= desiredMinimalDistance)
    {
      separatingForce += offset / distance;
    }
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