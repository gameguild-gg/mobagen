#ifndef SEPARATIONRULE_H
#define SEPARATIONRULE_H

#include "FlockingRule.h"

class SeparationRule : public FlockingRule {
private:
  float desiredMinimalDistance = 50;

public:
  explicit SeparationRule(float desiredSeparation = 20.f, float weight = 1.f, bool isEnabled = true)
      : FlockingRule(Color::Red, weight, isEnabled), desiredMinimalDistance(desiredSeparation) {}

  SeparationRule(const SeparationRule& toCopy) : FlockingRule(toCopy) { desiredMinimalDistance = toCopy.desiredMinimalDistance; }

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<SeparationRule>(*this); }

  const char* getRuleName() override { return "Separation Rule"; }
  const char* getRuleExplanation() override { return "Steer to avoid collision with nearby boids."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) override;
  bool drawImguiRuleExtra() override;
};

#endif
