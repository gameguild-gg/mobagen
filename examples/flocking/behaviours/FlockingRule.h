#ifndef FLOCKINGRULE_H
#define FLOCKINGRULE_H

#include <vector>
#include <memory>
#include "Vector2.h"
#include "Vector3.h"

class Boid;
class World;

class FlockingRule {
public:
    World* world;

protected:
    //We'll cache the computed force to be able to display it later
    Vector2 force;

    //if displayed
    Vector3 debugColor;

    explicit FlockingRule(World* pWorld, Vector3 debugColor_, float weight_, bool isEnabled_ = true) :
            debugColor(debugColor_),
            force(Vector2()),
            weight(weight_),
            isEnabled(isEnabled_),
            world(pWorld)
    {}

    virtual Vector2 computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) = 0;

    //Multiplier for weight so we can tilt values closer to each other
    virtual float getBaseWeightMultiplier() {
        return 1.;
    };

    //Name of the rule
    virtual const char* getRuleName() = 0;

    //Short explanation of the rule
    virtual const char* getRuleExplanation() = 0;

    //Function to override to draw extra tweakable settings on ImGui, depending on rule.
    virtual bool drawImguiRuleExtra() { return false; };

public:
    float weight;
    bool isEnabled;

    //Copy constructor
    FlockingRule(const FlockingRule& toCopy);

    virtual std::unique_ptr<FlockingRule> clone() = 0;

    //Compute the force, weight it, and save it in cache.
    Vector2 computeWeightedForce(const std::vector<Boid*>& neighborhood, Boid* boid);

    //Draw the core of the rule settings on ImGui.
    virtual bool drawImguiRule();

    // todo: probably we need to call this and change boid to particle
    // Inherited via Drawable
    virtual void draw(const Boid &boid, SDL_Renderer* renderer) const;
};

#endif
