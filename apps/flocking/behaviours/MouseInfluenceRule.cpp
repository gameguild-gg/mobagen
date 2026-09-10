#include "MouseInfluenceRule.h"
#include "imgui.h"

glm::vec2 MouseInfluenceRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 force(0.f);

  // ImGui::IsMouseDown(ImGuiMouseButton_Left) returns true if the left mouse button is currently pressed.
  // ImGui::GetIO().MousePos returns the current mouse position as an ImVec2.
  // glm::length(vec) returns the length of a vector

  // begin solution

  if (isRepulsive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    glm::vec2 vector = glm::vec2(0, 0);
    glm::vec2 normal = glm::vec2(0, 0);
    float length = 0;

    vector = boid.position - glm::vec2(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
    length = glm::length(vector);
    normal = normalize(vector);
    if (length > 0.0f && length <= 250.0f)
      force = normal * (250.0f / length);
  }

  if (!isRepulsive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    glm::vec2 vector = glm::vec2(0, 0);
    glm::vec2 normal = glm::vec2(0, 0);
    float length = 0;

    vector = glm::vec2(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y) - boid.position;
    length = sqrt(vector.x * vector.x + vector.y * vector.y);
    normal = normalize(vector);
    if (length > 0.0f && length <= 250.0f)
      force = normal * (250.0f / length);
  }

  // end solution

  return force;
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
