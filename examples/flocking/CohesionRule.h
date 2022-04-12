#ifndef COHESIONRULE_H
#define COHESIONRULE_H

class Boid;

/* Steer to move toward center of mass of local flockmates */
class CohesionRule : public FlockingRule
{

public:

    CohesionRule(float weight = 1., bool isEnabled = true) : FlockingRule(sf::Color::Cyan, weight, isEnabled)  {}

    std::unique_ptr<FlockingRule> clone() override
    {
        // Créer un pointeur concret en utilisant le constructeur abstrait parent
        return std::make_unique<CohesionRule>(*this);
    }

    const char* getRuleName() override
    {
        return "Cohesion Rule";
    }

    const char* getRuleExplanation() override
    {
        return "Steer to move toward center of mass of nearby boids.";
    }

    virtual float getBaseWeightMultiplier() override
    {
        return 1.;
    }

    sf::Vector2f computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) override;
};

#endif
