#ifndef WINDRULE_H
#define WINDRULE_H

#include "FlockingRule.h"

class WindRule : public FlockingRule {
private:
  float windAngle;

public:
  explicit WindRule(float weight = 1.f, float angle = 0.f, bool isEnabled = true) : FlockingRule(Color::White, weight, isEnabled), windAngle(angle) {}

  WindRule(const WindRule& toCopy) : FlockingRule(toCopy) { windAngle = toCopy.windAngle; }

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<WindRule>(*this); }

  const char* getRuleName() override { return "Wind Force"; }
  const char* getRuleExplanation() override { return "Apply a constant force to all boids."; }
  float getBaseWeightMultiplier() override { return 0.5f; }

  glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) override;
  bool drawImguiRuleExtra() override;
};

#endif
