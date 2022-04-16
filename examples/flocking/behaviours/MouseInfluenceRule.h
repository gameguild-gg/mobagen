#ifndef MOUSEINFLUENCERULE_H
#define MOUSEINFLUENCERULE_H

#include "FlockingRule.h"

class MouseInfluenceRule : public FlockingRule {
private:
    //If not avoiding, is attracted
    bool isRepulsive;

public:

    explicit MouseInfluenceRule(World* pWorld, float weight = 1., bool isRepulsive_ = false, bool isEnabled = true) : FlockingRule(pWorld, Vector3::Magenta(), weight, isEnabled), isRepulsive(isRepulsive_)
    {}

    MouseInfluenceRule(const MouseInfluenceRule& toCopy) : FlockingRule(toCopy) {
        isRepulsive = toCopy.isRepulsive;
    }

    std::unique_ptr<FlockingRule> clone() override {
        return std::make_unique<MouseInfluenceRule>(*this);
    }

    const char* getRuleName() override{
        return "Mouse Click Influence";
    }

    const char* getRuleExplanation() override {
        return "Steer toward or away the mouse when clicked.";
    }

    virtual float getBaseWeightMultiplier() override {
        return 0.1;
    }

    Vector2 computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) override;

    bool drawImguiRuleExtra() override;
};

#endif
