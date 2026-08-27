#include "WindRule.h"
#include "imgui.h"

glm::vec2 WindRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) { return glm::vec2(0.f); }

bool WindRule::drawImguiRuleExtra() {
  bool valueHasChanged = false;
  if (ImGui::SliderAngle("Wind Direction", &windAngle, 0)) {
    valueHasChanged = true;
  }
  return valueHasChanged;
}
