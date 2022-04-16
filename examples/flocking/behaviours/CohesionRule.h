#ifndef COHESIONRULE_H
#define COHESIONRULE_H

#include "FlockingRule.h"

class Boid;

/* Steer to move toward center of mass of local flockmates */
class CohesionRule : public FlockingRule {
public:

    explicit CohesionRule(World* pWorld, float weight = 1., bool isEnabled = true) : FlockingRule(pWorld,Vector3::Cyan(), weight, isEnabled)  {}

    std::unique_ptr<FlockingRule> clone() override {
        // Créer un pointeur concret en utilisant le constructeur abstrait parent
        return std::make_unique<CohesionRule>(*this);
    }

    const char* getRuleName() override {
        return "Cohesion Rule";
    }

    const char* getRuleExplanation() override {
        return "Steer to move toward center of mass of nearby boids.";
    }

    virtual float getBaseWeightMultiplier() override {
        return 1.;
    }

    Vector2 computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) override;
};

#endif
