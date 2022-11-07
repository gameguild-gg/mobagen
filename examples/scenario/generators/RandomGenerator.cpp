#include "RandomGenerator.h"
#include "../Noise.h"
std::vector<Color32> RandomScenarioGenerator::Generate(int sideSize) {
  Noise n = Noise(0,100,0,255);
  std::vector<Color32> colors;
  colors.reserve(sideSize*sideSize);
  for(int l=0;l<sideSize;l++){
    for(int c=0;c<sideSize;c++){
      auto color = n.noise(l/10.0,c/10.0); // this noise function needs to be improved
      colors.push_back(Color32(color,color,color));
    }
  }
  return colors;
}
std::string RandomScenarioGenerator::GetName() { return "EXAMPLE"; }
