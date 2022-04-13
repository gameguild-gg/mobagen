#include "Boid.h"
#include "behaviours/FlockingRule.h"
#include <iostream>

using namespace utils;

std::vector<Boid*> Boid::computeBoidNeighbordhood()
{
    std::vector<Boid*> neighborhood;

    float detectionRadiusSquared = detectionRadius * detectionRadius;
    auto position = getPosition();

    //We compare distance to squared distances to avoid doing square roots.

    for (const auto& boid : *boids)
    {
        if (boid != this) {

            float squareDistance = Vector2::getSquaredDistance(position, boid->getPosition());

            //Verify if boid is close enough to be part of the neighborhood
            if (squareDistance <= detectionRadiusSquared)
            {
                neighborhood.push_back(boid);
            }
        }

    }

    return neighborhood;
}


Boid::Boid(Engine *pEngine, std::vector<Boid*>* boids_) : Particle(engine), boids(boids_) {

}

void Boid::Update(const float deltaTime) {
    Particle::Update(deltaTime);

    std::vector<Boid*> neighbordhood = computeBoidNeighbordhood();

    for (auto& rule : rules)
    {
        auto weightedForce = rule->computeWeightedForce(neighbordhood, this);
        //std::cout << typeid(*rule).name() << " Force : " << vector2::getMagnitude(weightedForce) << std::endl;
        applyForce(weightedForce);
    }
}

void Boid::OnDraw(SDL_Renderer *renderer) {
    Particle::OnDraw(renderer); // super()

    if (drawDebugRadius) {
        //Display radius detection
        sf::CircleShape vision = sf::CircleShape(detectionRadius);

        vision.setFillColor(sf::Color::Transparent);
        vision.setOutlineThickness(1.);
        vision.setOrigin(vision.getRadius(), vision.getRadius());
        vision.setOutlineColor(sf::Color::Blue);
        vision.setPosition(shape.getPosition());

        target.draw(vision, states);
    }

    if (drawDebugRules)
    {

        //Display rules

        for (auto& rule : rules)
        {
            if (rule->isEnabled)
            {
                rule->draw(*this, target, states);
            }
        }

    }
}
