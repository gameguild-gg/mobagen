#include "AlignmentRule.h"
#include "../gameobjects/Boid.h"

Vector2f AlignmentRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
  // Try to match the heading of neighbors = Average velocity
  Vector2f averageVelocity = Vector2f::zero();
  float boidsInRange = 0;
  for (int i=0; i<neighborhood.size(); i++) {
    if ((boid->getPosition() - neighborhood[i]->getPosition()).getMagnitude() <= boid->getDetectionRadius()) {
       averageVelocity = averageVelocity + neighborhood[i]->getVelocity();
      boidsInRange++;
    }
  }
  if (boidsInRange!=0) {
Vector2f avgForce = averageVelocity / boidsInRange;
Vector2f forceApplied = boid->getVelocity() - avgForce;
    return forceApplied;
  }
  return Vector2f::zero();
  // todo: add your code here to align each boid in a neighborhood
  // hint: iterate over the neighborhood

  return Vector2f::normalized(averageVelocity);
}