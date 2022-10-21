#ifndef MOBAGEN_WORLD_H
#define MOBAGEN_WORLD_H

#include "GameObject.h"
#include "Node.h"
#include "Point2D.h"
#include <vector>

class World: GameObject {
 private:
  int sideSize;

  // .=
  // |
  // even indexes are top elements;
  // odd indexes are left elements;
  std::vector<bool> data;
 public:
  explicit World(Engine* pEngine, int size);

  Node GetNode(const Point2D& point);
  void SetNode(const Point2D& point, const Node& node);
  void SetNorth(const Point2D& point, bool state);
  void SetEast(const Point2D& point, bool state);
  void SetSouth(const Point2D& point, bool state);
  void SetWest(const Point2D& point, bool state);

  void Start() override;
  void OnGui(ImGuiContext *context) override;
  void OnDraw(SDL_Renderer* renderer) override;
  void Update(float deltaTime) override;
};

#endif
