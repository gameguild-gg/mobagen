#ifndef MOBAGEN_PARTICLEGENERATOR_H
#define MOBAGEN_PARTICLEGENERATOR_H

// reference: https://github.com/weigert/SimpleErosion
#include "../GeneratorBase.h"
#include <glm/vec3.hpp>
#include <glm/gtx/normal.hpp>
class ParticleGenerator: public ScenarioGeneratorBase {
 private:
  float heights[512][512];
  int sideSizeCached=0;
  glm::vec3 surfaceNormal(int i, int j);
  std::vector<Color32> heightsToColor();
  void generateRandomHeights(float displacement);
  void Erode(float dt);
  float minVol = 0.01;
  float density = 1;
  float friction = 0.05;
  float depositionRate = 0.1;
  float evapRate = 0.001;
  float lastUpdate=0;
 public:
  std::vector<Color32> Generate(int sideSize, float displacement=0) override;
  std::string GetName() override;
};

struct Particle{
  //Construct Particle at Position
  Particle(glm::vec2 _pos){ pos = _pos; }

  glm::vec2 pos;
  glm::vec2 speed = glm::vec2(0.0);

  float volume = 1.0;   //This will vary in time
  float sediment = 0.0; //Fraction of Volume that is Sediment!
};

#endif  // MOBAGEN_PARTICLEGENERATOR_H
