
#ifndef BOID_H
#define BOID_H

#include "FlockingRule.h"
#include "Pacticle.h"

class Boid : Particle
{

private:

    //Reference to all particles

    std::vector<Boid*>* boids;

    float detectionRadius = 100.;

    std::vector<std::unique_ptr<FlockingRule>> rules;

    //Methods
    std::vector<Boid*> computeBoidNeighbordhood();

public:

    //Member
    bool drawDebugRadius = true;
    bool drawDebugRules = true;

    //Constructor
    Boid(std::vector<Boid*>* boids_);

    //Getter - Setters
    void setFlockingRules(std::vector<std::unique_ptr<FlockingRule>> const& newRules)
    {
        rules.clear();

        //Clone the rules in newRules in the boid rules.

        for (auto& rule : newRules)
        {
            rules.push_back(rule->clone());
        }
    }

    void setDetectionRadius(float newRadius)
    {
        detectionRadius = newRadius;
    }

    float getDetectionRadius() const
    {
        return detectionRadius;
    }

    void update(const float deltaTime) override;


    // Inherited via Drawable
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states = sf::RenderStates::Default) const override;

};



#endif
