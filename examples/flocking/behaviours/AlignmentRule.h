#ifndef MOBAGEN_ALIGNMENTRULE_H
#define MOBAGEN_ALIGNMENTRULE_H

#include "FlockingRule.h"

/* Steer toward average heading of local flockmates */
class AlignmentRule : public FlockingRule {
public:
    explicit AlignmentRule(World* pWorld, float weight = 1., bool isEnabled = true) : FlockingRule(pWorld, Vector3::Yellow(), weight, isEnabled) {}

    std::unique_ptr<FlockingRule> clone() override {
        return std::make_unique<AlignmentRule>(*this);
    }

    const char* getRuleName() override {
        return "Alignment Rule";
    }

    const char* getRuleExplanation() override {
        return "Steer to move in the same direction that nearby boids.";
    }

    virtual float getBaseWeightMultiplier() override {
        return 1.;
    }

    Vector2 computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) override;
};

#endif
