#ifndef SEPARATIONRULE_H
#define SEPARATIONRULE_H

/* Steer to avoid local flockmates */
class SeparationRule : public FlockingRule
{
private:
    float desiredMinimalDistance = 10;

public:

    SeparationRule(float desiredSeparation = 20., float weight = 1., bool isEnabled = true) :
            FlockingRule(sf::Color::Red, weight, isEnabled), desiredMinimalDistance(desiredSeparation)
    {}

    std::unique_ptr<FlockingRule> clone() override
    {
        return std::make_unique<SeparationRule>(*this);
    }

    const char* getRuleName() override
    {
        return "Separation Rule";
    }

    const char* getRuleExplanation() override
    {
        return "Steer to avoid collision with nearby boids.";
    }

    virtual float getBaseWeightMultiplier() override
    {
        return 1.;
    }


    sf::Vector2f computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) override;

    bool drawImguiRuleExtra() override;

};

#endif
