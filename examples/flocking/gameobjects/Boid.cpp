#include "Boid.h"
#include "World.h"

std::vector<Boid*> Boid::computeBoidNeighborhood() {
    std::vector<Boid*> neighborhood;

    float detectionRadiusSquared = detectionRadius * detectionRadius;
    auto position = getPosition();

    // We compare distance to squared distances to avoid doing square roots.
    // TODO: Optimize this! Move this to the world manager
    // Option 1. Locality Sensitive Hashing or Spatial hashing
    // Option 2. Quadtree or octree
    for (const auto& boid : world->boids) {
        if (boid != this) {
            float squareDistance = Vector2::getSquaredDistance(position, boid->getPosition());

            //Verify if boid is close enough to be part of the neighborhood
            if (squareDistance <= detectionRadiusSquared)
                neighborhood.push_back(boid);
        }
    }

    return neighborhood;
}


Boid::Boid(Engine *pEngine, World *pWorld) : Particle(pEngine), world(pWorld) {}

void Boid::Update(float deltaTime) {
    Particle::Update(deltaTime);
    std::vector<Boid*> neighborhood = computeBoidNeighborhood();

    for (auto& rule : rules)
    {
        auto weightedForce = rule->computeWeightedForce(neighborhood, this);
        //std::cout << typeid(*rule).name() << " Force : " << vector2::getMagnitude(weightedForce) << std::endl;
        applyForce(weightedForce);
    }
}

void Boid::OnDraw(SDL_Renderer *renderer) {
    if (drawDebugRadius)
        circle.Draw(
                renderer,
                transform.position,
                {detectionRadius,detectionRadius},
                Vector2::zero(),
                circleColor);

    //Display rules
    if (drawDebugRules)
        for (auto& rule : rules)
            if (rule->isEnabled)
                rule->draw(*this, renderer);

    Particle::OnDraw(renderer); // super()
}
