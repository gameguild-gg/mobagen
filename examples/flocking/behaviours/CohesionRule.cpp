#include "CohesionRule.h"
#include "../gameobjects/Boid.h"

Vector2f CohesionRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
  Vector2f cohesionForce;

  // todo: add your code here to make a force towards the center of mass
  // hint: iterate over the neighborhood

  // find center of mass
  Vector2f centerOfMass = Vector2f::zero();
  for (auto b : neighborhood) centerOfMass += b->getPosition();  // accumulate positions
  centerOfMass /= neighborhood.size();                           // divide by number of boids

  cohesionForce = centerOfMass - boid->getPosition();  // direction force towards center of mass

  return cohesionForce;
}