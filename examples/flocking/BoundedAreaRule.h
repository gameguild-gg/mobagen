#ifndef BOUNDEDAREARULE_H
#define BOUNDEDAREARULE_H

class BoundedAreaRule : public FlockingRule
{
private:
    int widthWindows;
    int heightWindows;

    int desiredDistance;

public:

    BoundedAreaRule(int heightWindows_, int widthWindows_, int distanceFromBorder_, float weight = 1., bool isEnabled = true) :
            FlockingRule(utils::graphics::LightRed, weight, isEnabled), widthWindows(widthWindows_), heightWindows(heightWindows_), desiredDistance(distanceFromBorder_)
    {}

    BoundedAreaRule(const BoundedAreaRule& toCopy) : FlockingRule(toCopy)
    {
        widthWindows = toCopy.widthWindows;
        heightWindows = toCopy.heightWindows;
        desiredDistance = toCopy.desiredDistance;
    }

    std::unique_ptr<FlockingRule> clone() override
    {
        return std::make_unique<BoundedAreaRule>(*this);
    }


    const char* getRuleName() override
    {
        return "Bounded Windows";
    }

    const char* getRuleExplanation() override
    {
        return "Steer to avoid the window's borders.";
    }

    virtual float getBaseWeightMultiplier() override
    {
        return 1.;
    }

    sf::Vector2f computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) override;

    bool drawImguiRuleExtra() override;


    virtual void draw(const Boid& boid, sf::RenderTarget& target, sf::RenderStates states = sf::RenderStates::Default) const override;

};

#endif
