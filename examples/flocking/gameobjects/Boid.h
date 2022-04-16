#ifndef BOID_H
#define BOID_H

#include "../behaviours/FlockingRule.h"
#include "Pacticle.h"
#include "Polygon.h"

class World;

class Boid : public Particle {
private:
    float detectionRadius = 100.;

    std::vector<std::unique_ptr<FlockingRule>> rules;

    //Methods
    std::vector<Boid*> computeBoidNeighborhood();

    Circle circle = Circle(12);
    World* world;

public:
    //Member
    bool drawDebugRadius = true;
    bool drawDebugRules = true;
    Vector3 circleColor = Vector3::Purple();

    //Constructor
    explicit Boid(Engine *pEngine, World* pWorld);

    //Getter - Setters
    void setFlockingRules(std::vector<std::unique_ptr<FlockingRule>> const& newRules) {
        rules.clear();

        //Clone the rules in newRules in the boid rules.
        for (auto& rule : newRules) {
            rules.push_back(rule->clone());
        }
    }

    void setDetectionRadius(float newRadius) {
        detectionRadius = newRadius;
    }

    float getDetectionRadius() const {
        return detectionRadius;
    }

    void Update(float deltaTime) override;

    // Inherited via Drawable
    virtual void OnDraw(SDL_Renderer* renderer) override;
};



#endif
