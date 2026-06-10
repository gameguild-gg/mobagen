#include "MouseInfluenceRule.h"
#include "../gameobjects/Boid.h"
#include "../gameobjects/World.h"
#include "engine/Engine.h"

Vector2f MouseInfluenceRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
  ImGui::SetCurrentContext(world->engine->window->imGuiContext);
  //    ImGuiIO& io = ImGui::GetIO();
  //    if (ImGui::IsMousePosValid() && io.MouseDown[0]) {
  //        Vector2f mousePos(io.MousePos.x, io.MousePos.y); // todo: use this
  //        Vector2f displacement = Vector2f::zero(); // todo: change this
  //        float distance = 0; // todo: change this
  //
  //        //The force is inversely proportional to distance
  //        Vector2f force = Vector2f::zero(); // todo: change this
  //
  //        if (isRepulsive)
  //            force *= -1.f;
  //
  //        return force;
  //    }
  //    else
  //        return Vector2f::zero();
  return Vector2f::zero();
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