#include "MouseInfluenceRule.h"
#include "imgui.h"

glm::vec2 MouseInfluenceRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 force(0.f);

  // begin solution

  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    return force;
  }

  ImVec2 mp = ImGui::GetIO().MousePos;
  glm::vec2 mousePos(mp.x, mp.y);

  glm::vec2 offset = mousePos - boid.position;
  float distance = glm::length(offset);

  if (distance > 0.0001f)
  {
    glm::vec2 direction = offset / distance;
    force = isRepulsive ? -direction : direction;
  }

  // end solution

  return force * 100.f;
}

bool MouseInfluenceRule::drawImguiRuleExtra() {
  bool valueHasChanged = false;

  if (ImGui::RadioButton("Attractive", !isRepulsive)) {
    isRepulsive = false;
    valueHasChanged = true;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Repulsive", isRepulsive)) {
    isRepulsive = true;
    valueHasChanged = true;
  }

  return valueHasChanged;
}