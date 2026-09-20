#ifndef SEPARATIONRULE_H
#define SEPARATIONRULE_H

#include "FlockingRule.h"

class SeparationRule : public FlockingRule {
public:
  float radius = 50.f;

  explicit SeparationRule(float radius = 50.f, float weight = 1.f, bool isEnabled = true)
      : FlockingRule(Color::Red, weight, isEnabled), radius(radius) {}

  SeparationRule(const SeparationRule& toCopy) : FlockingRule(toCopy) { radius = toCopy.radius; }

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<SeparationRule>(*this); }

  const char* getRuleName() override { return "Separation Rule"; }
  const char* getRuleExplanation() override { return "Steer to avoid collision with nearby boids."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& boids, int selfIndex) override;
  bool drawImguiRuleExtra() override;
  void drawRadius(const BoidView& boid, ImDrawList* dl) const override;
};

#endif
