#ifndef BOUNDEDAREARULE_H
#define BOUNDEDAREARULE_H

#include "FlockingRule.h"

class BoundedAreaRule : public FlockingRule {
private:
    int desiredDistance;

public:
    BoundedAreaRule(World* pWorld, int distanceFromBorder_, float weight = 1., bool isEnabled = true) :
            FlockingRule(pWorld, Vector3::LightRed(), weight, isEnabled), desiredDistance(distanceFromBorder_)
    {}

    BoundedAreaRule(const BoundedAreaRule& toCopy) : FlockingRule(toCopy) {
        desiredDistance = toCopy.desiredDistance;
    }

    std::unique_ptr<FlockingRule> clone() override {
        return std::make_unique<BoundedAreaRule>(*this);
    }


    const char* getRuleName() override {
        return "Bounded Windows";
    }

    const char* getRuleExplanation() override {
        return "Steer to avoid the window's borders.";
    }

    virtual float getBaseWeightMultiplier() override {
        return 1.;
    }

    Vector2 computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) override;

    bool drawImguiRuleExtra() override;

    virtual void draw(const Boid& boid, SDL_Renderer* renderer) const override;
};

#endif
