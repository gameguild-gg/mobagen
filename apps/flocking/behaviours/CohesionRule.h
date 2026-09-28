#ifndef COHESIONRULE_H
#define COHESIONRULE_H

#include "FlockingRule.h"

class CohesionRule : public FlockingRule {
public:
  float radius = 100.f;

  explicit CohesionRule(float radius = 100.f, float weight = 1.f, bool isEnabled = true)
      : FlockingRule(Color::Cyan, weight, isEnabled), radius(radius) {}

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<CohesionRule>(*this); }

  const char* getRuleName() override { return "Cohesion Rule"; }
  const char* getRuleExplanation() override { return "Steer to move toward center of mass of nearby boids."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& boids, int selfIndex) override;
  bool drawImguiRuleExtra() override;
  void drawRadius(const BoidView& boid, ImDrawList* dl) const override;
};

#endif
