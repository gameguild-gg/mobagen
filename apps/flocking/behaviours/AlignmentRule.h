#ifndef MOBAGEN_ALIGNMENTRULE_H
#define MOBAGEN_ALIGNMENTRULE_H

#include "FlockingRule.h"

class AlignmentRule : public FlockingRule {
public:
  float radius = 100.f;

  explicit AlignmentRule(float radius = 100.f, float weight = 1.f, bool isEnabled = true)
      : FlockingRule(Color::Yellow, weight, isEnabled), radius(radius) {}

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<AlignmentRule>(*this); }

  const char* getRuleName() override { return "Alignment Rule"; }
  const char* getRuleExplanation() override { return "Steer to move in the same direction that nearby boids."; }
  float getBaseWeightMultiplier() override { return 1.f; }

  glm::vec2 computeForce(const std::vector<BoidView>& boids, int selfIndex) override;
  bool drawImguiRuleExtra() override;
  void drawRadius(const BoidView& boid, ImDrawList* dl) const override;
};

#endif
