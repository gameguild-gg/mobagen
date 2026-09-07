#include "MouseInfluenceRule.h"
#include "imgui.h"

glm::vec2 MouseInfluenceRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  glm::vec2 force(0.f);

  // ImGui::IsMouseDown(ImGuiMouseButton_Left) returns true if the left mouse button is currently pressed.
  // ImGui::GetIO().MousePos returns the current mouse position as an ImVec2.
  // glm::length(vec) returns the length of a vector

  // begin solution

  glm::vec2 separatingForce(0.f);

  if (!isRepulsive)
  {
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
  }

  if (isRepulsive)
  {
    if (numOfNeighbours != 0)
    {
      if (sqrt(totalForces.x * totalForces.x + totalForces.y * totalForces.y) > weight)
        totalForces = normalize(totalForces) * weight;

      separatingForce = totalForces;

      glm::vec2 positionTotal = glm::vec2(0, 0);
      glm::vec2 forceNeeded = glm::vec2(0, 0);
      int numOfNeighbours = 0;
    }

    for (const BoidView& boidInRange : neighborhood)
    {
      positionTotal += boidInRange.position;
      numOfNeighbours++;
    }

    if (numOfNeighbours != 0)
    {
      glm::vec2 centerMass = glm::vec2(positionTotal.x / numOfNeighbours, positionTotal.y / numOfNeighbours);
      forceNeeded = centerMass - boid.position;
      forceNeeded = glm::normalize(forceNeeded);
    }

    // end solution
    cohesionForce = forceNeeded;
    return cohesionForce;
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
