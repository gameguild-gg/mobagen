#include "RandomGenerator.h"
std::vector<Color32> RandomScenarioGenerator::Generate(int sideSize) {
  std::vector<Color32> colors;
  colors.resize(sideSize*sideSize);
  for(int i=0; i<sideSize*sideSize;i++){
    colors[i] = Color32::RandomColor();
  }
}
std::string RandomScenarioGenerator::GetName() { return "EXAMPLE"; }
