#ifndef BOID_H
#define BOID_H

#include <glm/glm.hpp>
#include "math/ColorT.h"
#include <vector>

struct BoidPos {
  glm::vec2 pos{0.f};
};

struct BoidVel {
  glm::vec2 vel{0.f};
};

struct BoidAcc {
  glm::vec2 acc{0.f};
  glm::vec2 prevAcc{0.f};
};

struct BoidConfig {
  float detectionRadius = 100.f;
  float speed = 120.f;
  bool hasConstantSpeed = false;
  float maxAcceleration = 10.f;
};

struct BoidDebug {
  bool drawDebugRadius = false;
  bool drawDebugRules = false;
  bool drawAcceleration = false;
  Color32 color;
};

struct BoidForceCache {
  std::vector<glm::vec2> forces;
};

#endif
