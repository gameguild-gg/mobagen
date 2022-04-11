#ifndef WORLD_H
#define WORLD_H

#include "FlockingRule.h"
#include "Boid.h"

class Particle;

using BoidPtr = std::unique_ptr<Boid>;



class World
{
private:

    /// MEMBERS

    /// Configuration

    //Boids
    int nbBoids = 300;

    bool hasConstantSpeed = false;
    float desiredSpeed = 120.;

    bool hasMaxAcceleration = false;
    float maxAcceleration = 10.;

    float detectionRadius = 40.;

    //Display
    bool showRadius = false;
    bool showRules = false;
    bool showAcceleration = false;

    //rules
    std::vector<std::unique_ptr<FlockingRule>> boidsRules;
    float* defaultWeights; //array of default rules weight

    /// Particles
    std::vector<BoidPtr> boids; //owned particles

    //cached list to manipulate objects
    std::vector<Boid*> cachedBoids;


    sf::RenderWindow* windowPtr;

    /// METHODS

    void initializeRules();

    //Boids

    void applyFlockingRulesToAllBoids();
    void setNumberOfBoids(int number);
    void randomizeBoidPositionAndVelocity(Boid* boid);
    void warpParticleIfOutOfBounds(Particle* particle);

    BoidPtr createBoid();

public:

    /// METHODS
    World(sf::RenderWindow* windowPtr_);

    //Getters
    std::vector<Boid*>* getAllBoids();

    int getNbBoids()
    {
        return nbBoids;
    }

    //Setters

    //ImGui
    void drawGeneralUI();
    void drawRulesUI();

    //Update
    void update(float deltaTime);
    void updatePositions(float deltaTime);
};


#endif
