#ifndef WINDRULE_H
#define WINDRULE_H

#include "FlockingRule.h"

class WindRule : public FlockingRule
{
private:
    float windAngle;

public:

    explicit WindRule(World* pWorld, float weight = 1., float angle = 0, bool isEnabled = true) : FlockingRule(pWorld, Vector3::White(), weight, isEnabled), windAngle(angle)
    {}

    WindRule(const WindRule & toCopy) : FlockingRule(toCopy) {
        windAngle = toCopy.windAngle;
    }

    std::unique_ptr<FlockingRule> clone() override {
        return std::make_unique<WindRule>(*this);
    }

    const char* getRuleName() override {
        return "Wind Force";
    }

    const char* getRuleExplanation() override {
        return "Apply a constant force to all boids.";
    }

    virtual float getBaseWeightMultiplier() override {
        return 0.5;
    }

    Vector2 computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) override;

    bool drawImguiRuleExtra() override;
};


#endif
