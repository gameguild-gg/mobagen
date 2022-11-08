#include "RandomGenerator.h"
#include "../PerlinNoise.hpp"
#include <iostream>
std::vector<Color32> RandomScenarioGenerator::Generate(int sideSize) {
  std::vector<Color32> colors;
  siv::BasicPerlinNoise<float> noise;
  noise.reseed(1337);
  for(int l=0; l<sideSize; l++){
    for(int c=0;c<sideSize; c++){
      float rgb = abs(noise.noise2D(c/50.0,l/50.0)*255);
      colors.emplace_back(rgb, rgb, rgb);
    }
  }
  std::cout<<colors.size() << std::endl;
  return colors;
}
std::string RandomScenarioGenerator::GetName() { return "EXAMPLE"; }
