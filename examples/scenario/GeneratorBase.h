#ifndef SCENARIOGENERATORBASE_H
#define SCENARIOGENERATORBASE_H
#include <ColorT.h>
#include <string>
#include <vector>

class ScenarioGeneratorBase {
 public:
  virtual std::vector<Color32> Generate(int sideSize)=0;
  virtual std::string GetName()=0;
};

#endif  // SCENARIOGENERATORBASE_H
