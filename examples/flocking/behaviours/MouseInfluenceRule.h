#ifndef MOUSEINFLUENCERULE_H
#define MOUSEINFLUENCERULE_H


class MouseInfluenceRule : public FlockingRule
{

private:

    //If not avoiding, is attracted
    bool isRepulsive;



public:

    MouseInfluenceRule(float weight = 1., bool isRepulsive_ = false, bool isEnabled = true) : FlockingRule(sf::Color::Magenta, weight, isEnabled), isRepulsive(isRepulsive_)
    {}

    MouseInfluenceRule(const MouseInfluenceRule& toCopy) : FlockingRule(toCopy)
    {
        isRepulsive = toCopy.isRepulsive;
    }

    std::unique_ptr<FlockingRule> clone() override
    {
        return std::make_unique<MouseInfluenceRule>(*this);
    }


    const char* getRuleName() override
    {
        return "Mouse Click Influence";
    }

    const char* getRuleExplanation() override
    {
        return "Steer toward or away the mouse when clicked.";
    }

    virtual float getBaseWeightMultiplier() override
    {
        return 0.1;
    }


    sf::Vector2f computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) override;

    bool drawImguiRuleExtra() override;

};


#endif
