#include "WindRule.h"
#include "imgui.h"
#include <cmath>

glm::vec2 WindRule::computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) {
  // std::cos and std::sin return the cosine and sine of an angle in radians, respectively.
  // windAngle is the angle of the wind direction in degrees, so we need to convert it to radians by multiplying it by (pi / 180).
  
  // begin solution
  float angleInRadians = windAngle /* (3.14159/ 180.f)*/;  
  return glm::vec2(cos(angleInRadians), sin(angleInRadians));
  // end solution
}

bool WindRule::drawImguiRuleExtra() {
  bool valueHasChanged = false;
  if (ImGui::SliderAngle("Wind Direction", &windAngle, 0)) {
    valueHasChanged = true;
  }
  return valueHasChanged;
}
