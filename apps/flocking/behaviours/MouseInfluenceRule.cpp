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
    glm::vec2 hat = glm::vec2(0, 0);
    double magnitude = 0;

    vector = boid.position - glm::vec2(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
    magnitude = sqrt(vector.x * vector.x + vector.y * vector.y);
    hat = normalize(vector);
    if (magnitude > 0.0f && magnitude <= 250.0f)
      force = hat * (250.0f / float(magnitude));
  }

  if (!isRepulsive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    glm::vec2 vector = glm::vec2(0, 0);
    glm::vec2 hat = glm::vec2(0, 0);
    double magnitude = 0;

    vector = glm::vec2(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y) - boid.position;
    magnitude = sqrt(vector.x * vector.x + vector.y * vector.y);
    hat = normalize(vector);
    if (magnitude > 0.0f && magnitude <= 250.0f)
      force = hat * (250.0f / float(magnitude));
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
