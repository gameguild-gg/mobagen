#ifndef MOUSEINFLUENCERULE_H
#define MOUSEINFLUENCERULE_H

#include "FlockingRule.h"

class MouseInfluenceRule : public FlockingRule {
private:
  bool isRepulsive;

public:
  explicit MouseInfluenceRule(float weight = 1.f, bool isRepulsive_ = false, bool isEnabled = true)
      : FlockingRule(Color::Magenta, weight, isEnabled), isRepulsive(isRepulsive_) {}

  MouseInfluenceRule(const MouseInfluenceRule& toCopy) : FlockingRule(toCopy) { isRepulsive = toCopy.isRepulsive; }

  std::unique_ptr<FlockingRule> clone() override { return std::make_unique<MouseInfluenceRule>(*this); }

  const char* getRuleName() override { return "Mouse Click Influence"; }
  const char* getRuleExplanation() override { return "Steer toward or away the mouse when clicked."; }
  float getBaseWeightMultiplier() override { return 0.1f; }

  glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) override;
  bool drawImguiRuleExtra() override;
};

#endif
