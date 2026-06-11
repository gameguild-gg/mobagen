#ifndef WORLD_H
#define WORLD_H

#include "../behaviours/FlockingRule.h"
#include "Boid.h"
#include "ecs/world.hpp"
#include "jobs/scheduler.hpp"

#include <memory>
#include <vector>

class FlockingManager {
private:
  ecs::World& ecs_;
  jobs::Scheduler& sched_;

  int nbBoids = 300;
  bool hasConstantSpeed = false;
  float desiredSpeed = 120.0f;
  bool hasMaxAcceleration = false;
  float maxAcceleration = 10.0f;
  float detectionRadius = 35.f;

  bool showRadius = false;
  bool showRules = false;
  bool showAcceleration = false;

  std::vector<std::unique_ptr<FlockingRule>> boidsRules;
  std::vector<float> defaultWeights;
  std::vector<ecs::Entity> boidEntities;

  void initializeRules();
  void setNumberOfBoids(int number);
  ecs::Entity createBoid();
  void randomizeBoidPosVel(ecs::Entity e);
  void warpIfOutOfBounds(BoidPos& p);

  void drawGeneralUI();
  void drawRulesUI();
  void drawPerformanceUI(float deltaTime);
  void showConfigurationWindow(float deltaTime);

public:
  explicit FlockingManager(ecs::World& world, jobs::Scheduler& sched);

  void Start();
  void Update(float deltaTime);
  void OnGui();
  void OnDraw();
};

#endif
