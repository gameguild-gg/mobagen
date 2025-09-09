#include "SeparationRule.h"
#include "../gameobjects/Boid.h"
#include "../gameobjects/World.h"
#include "engine/Engine.h"

Vector2f SeparationRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
  // Try to avoid boids too close
  Vector2f separatingForce = Vector2f::zero();

  //    float desiredDistance = desiredMinimalDistance;
  //
  // todo: implement a force that if neighbor(s) enter the radius, moves the boid away from it/them
      if (!neighborhood.empty()) {
       Vector2f position = boid->transform.position;
          int countCloseFlockmates = 0;
          // todo: find and apply force only on the closest mates
     }

  separatingForce = Vector2f::normalized(separatingForce);
  Vector2 sepForce = {0.0,0.0};
  double boidsInRangeCount = 0.0;
  for (int i = 0; i < neighborhood.size(); ++i) {
      Vector2 distance =(boid->getPosition() - neighborhood[i]->getPosition());
        Vector2 rep = distance.normalized() / distance.getMagnitude();
        separatingForce += rep;
        //Vector2 separation = {direction.x * k/(distance.x), direction.y * k/(distance.y)};
        boidsInRangeCount += 1.0;
  }
  if (!neighborhood.empty()) {
    Vector2 maxed = (separatingForce/neighborhood.size()) * boid->getDetectionRadius();
    return maxed;
  }
  return {0, 0};
}

bool SeparationRule::drawImguiRuleExtra() {
  ImGui::SetCurrentContext(world->engine->window->imGuiContext);
  bool valusHasChanged = false;

  if (ImGui::DragFloat("Desired Separation", &desiredMinimalDistance, 0.05f)) {
    valusHasChanged = true;
  }

  return valusHasChanged;
}
