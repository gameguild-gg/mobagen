#ifndef FLOCKINGRULE_H
#define FLOCKINGRULE_H

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "math/ColorT.h"
#include "imgui.h"

struct BoidView {
  glm::vec2 position{0.f};
  glm::vec2 velocity{0.f};
};

class FlockingRule {
protected:
  Color32 debugColor;

  explicit FlockingRule(Color32 debugColor_, float weight_, bool isEnabled_ = false)
      : debugColor(debugColor_), weight(weight_), isEnabled(isEnabled_) {}

  virtual glm::vec2 computeForce(const std::vector<BoidView>& neighborhood, const BoidView& boid) = 0;

  virtual float getBaseWeightMultiplier() { return 1.f; }

  virtual const char* getRuleName() = 0;
  virtual const char* getRuleExplanation() = 0;
  virtual bool drawImguiRuleExtra() { return false; }

public:
  float weight;
  bool isEnabled;

  FlockingRule(const FlockingRule& toCopy);
  virtual ~FlockingRule() = default;

  virtual std::unique_ptr<FlockingRule> clone() = 0;

  glm::vec2 computeWeightedForce(const std::vector<BoidView>& neighborhood, const BoidView& boid);

  virtual bool drawImguiRule();

  virtual void draw(const BoidView& boid, ImDrawList* dl, glm::vec2 cachedForce) const;

  virtual void drawWorldOverlay(ImDrawList* dl) const {}
};

#endif
