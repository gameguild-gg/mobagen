#ifndef MOBAGEN_EXAMPLES_SCENARIO_GENERATORS_WATEREROSION_H_
#define MOBAGEN_EXAMPLES_SCENARIO_GENERATORS_WATEREROSION_H_

#include "../GeneratorBase.h"

class WaterErosion: public ScenarioGeneratorBase {
 public:
  std::vector<Color32> Generate(int sideSize, float displacement=0) override;
  std::string GetName() override;
};

#endif //MOBAGEN_EXAMPLES_SCENARIO_GENERATORS_WATEREROSION_H_
