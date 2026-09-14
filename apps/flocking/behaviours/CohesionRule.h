#ifndef COHESIONRULE_H
#define COHESIONRULE_H

#include "FlockingRule.h"

class CohesionRule : public FlockingRule {
public:
  explicit CohesionRule(float weight = 1.f, bool isEnabled = false) : FlockingRule(Color::Cyan, weight, isEnabled) {}

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<CohesionRule>(*this); }

  const char* getRuleName() override { return "Cohesion Rule"; }
  const char* getRuleExplanation() override { return "Steer to move toward center of mass of nearby boids."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) override;
};

#endif
