#include "RandomGenerator.h"
#include "../Noise.h"
#include "../PerlinNoise.hpp"
#include <iostream>
// do not use this one to your assignment. this is my sample generator
std::vector<Color32> RandomScenarioGenerator::Generate(int sideSize, float displacement) {
  std::vector<Color32> colors;
//   create your own function for noise generation
  siv::BasicPerlinNoise<float> noise;
  noise.reseed(1337);
//  Noise noise(1337, 1024,0,255);
  for(int l=0; l<sideSize; l++){
    for(int c=0;c<sideSize; c++){
      float rgb = abs(noise.octave3D(c/50.0,l/50.0, displacement, 2)*255);
      colors.emplace_back(rgb, rgb, rgb);
//      double color = noise.noise(c/50.0,l/50.0);
//      colors.emplace_back(color,color,color);
    }
  }
  std::cout<<colors.size() << std::endl;
  return colors;
}
std::string RandomScenarioGenerator::GetName() { return "EXAMPLE"; }
