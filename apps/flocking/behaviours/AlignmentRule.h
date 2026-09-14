#ifndef MOBAGEN_ALIGNMENTRULE_H
#define MOBAGEN_ALIGNMENTRULE_H

#include "FlockingRule.h"

class AlignmentRule : public FlockingRule {
public:
  explicit AlignmentRule(float weight = 1.f, bool isEnabled = false) : FlockingRule(Color::Yellow, weight, isEnabled) {}

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<AlignmentRule>(*this); }

  const char* getRuleName() override { return "Alignment Rule"; }
  const char* getRuleExplanation() override { return "Steer to move in the same direction that nearby boids."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) override;
};

#endif
