#ifndef WORLD_H
#define WORLD_H

#include "behaviours/FlockingRule.h"
#include "Boid.h"
#include "GameObject.h"

class Particle;

using BoidPtr = std::unique_ptr<Boid>;



class World: GameObject {
private:
    /// MEMBERS
    /// Configuration
    // Boids
    int nbBoids = 300;

    bool hasConstantSpeed = false;
    float desiredSpeed = 120.0f;

    bool hasMaxAcceleration = false;
    float maxAcceleration = 10.0f;

    float detectionRadius = 40.04;

    //Display
    bool showRadius = false;
    bool showRules = false;
    bool showAcceleration = false;

    //rules
    std::vector<std::unique_ptr<FlockingRule>> boidsRules;
    float* defaultWeights; // array of default rules weight

    /// Particles
    std::vector<BoidPtr> boids; // owned particles

    //cached list to manipulate objects
    std::vector<Boid*> cachedBoids;

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
    explicit World(Engine* pEngine);

    //Getters
    std::vector<Boid*>* getAllBoids();

    int getNbBoids() const;

    //Setters

    //ImGui
    void drawGeneralUI();
    void drawRulesUI();

    //Update
    void Update(float deltaTime);
    void updatePositions(float deltaTime);

public:
    void showConfigurationWindow();
    void drawPerformanceUI();
};


#endif
