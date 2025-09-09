#include "CohesionRule.h"
#include "../gameobjects/Boid.h"

Vector2f CohesionRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
  Vector2f cohesionForce;
  float boidsInRange = 0.0f;
for (int i = 0; i < neighborhood.size(); i++) {
if (neighborhood[i]!=boid) {
if ((neighborhood[i]->getPosition() - boid->getPosition()).getMagnitude() <= boid->getDetectionRadius()) {
cohesionForce += neighborhood[i]->getPosition();
  boidsInRange++;
}
}
}
if (boidsInRange!= 0) {
  cohesionForce = (cohesionForce / boidsInRange);
Vector2f force =cohesionForce-boid->getPosition();
  return force.normalized();
}
  // hint: iterate over the neighborhood

  // find center of mass

  return cohesionForce;
}