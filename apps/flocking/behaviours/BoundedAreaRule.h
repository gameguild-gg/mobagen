#ifndef BOUNDEDAREARULE_H
#define BOUNDEDAREARULE_H

#include "FlockingRule.h"

class BoundedAreaRule : public FlockingRule {
private:
  int desiredDistance;

public:
  BoundedAreaRule(int distanceFromBorder_, float weight = 1.f, bool isEnabled = true)
      : FlockingRule(Color::Red.Light(), weight, isEnabled), desiredDistance(distanceFromBorder_) {}

  BoundedAreaRule(const BoundedAreaRule& toCopy) : FlockingRule(toCopy) { desiredDistance = toCopy.desiredDistance; }

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<BoundedAreaRule>(*this); }

  const char* getRuleName() override { return "Bounded Windows"; }
  const char* getRuleExplanation() override { return "Steer to avoid the window's borders."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) override;
  bool drawImguiRuleExtra() override;
  void drawWorldOverlay(ImDrawList* dl) const override;
};

#endif
