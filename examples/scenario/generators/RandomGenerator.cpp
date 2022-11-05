#include "RandomGenerator.h"
std::vector<Color32> RandomScenarioGenerator::Generate(int sideSize) {
  std::vector<Color32> colors;;
  for(int i=0; i<sideSize*sideSize;i++){
    colors.push_back(Color32::RandomColor());
  }
  return colors;
}
std::string RandomScenarioGenerator::GetName() { return "EXAMPLE"; }
