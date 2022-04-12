#ifndef MOBAGEN_ALIGNMENTRULE_H
#define MOBAGEN_ALIGNMENTRULE_H


/* Steer toward average heading of local flockmates */
class AlignmentRule : public FlockingRule
{

public:

    AlignmentRule(float weight = 1., bool isEnabled = true) : FlockingRule(sf::Color::Yellow, weight, isEnabled) {}



    std::unique_ptr<FlockingRule> clone() override
    {
        return std::make_unique<AlignmentRule>(*this);
    }

    const char* getRuleName() override
    {
        return "Alignment Rule";
    }

    const char* getRuleExplanation() override
    {
        return "Steer to move in the same direction that nearby boids.";
    }

    virtual float getBaseWeightMultiplier() override
    {
        return 1.;
    }

    sf::Vector2f computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) override;
};

#endif
