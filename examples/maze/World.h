#ifndef MOBAGEN_WORLD_H
#define MOBAGEN_WORLD_H

#include "GameObject.h"
#include "MazeGenerator.h"
#include "Node.h"
#include "Point2D.h"
#include <vector>

class World: GameObject {
 private:
  int sideSize;

  MazeGenerator generator;

  // .=
  // |
  // even indexes are top elements;
  // odd indexes are left elements;
  std::vector<bool> data;
  // convert a point into the index of the left vertex of the node
  inline int Point2DtoIndex(const Point2D& point){
    // todo: test. unstable interface
    auto sizeOver2 = sideSize/2;
    return (point.y + sizeOver2)*(sideSize+1)*2 + (point.x + sizeOver2)*2;
  }
 public:
  explicit World(Engine* pEngine, int size);

  inline Node GetNode(const Point2D& point);

  inline void GetNorth(const Point2D& point);
  inline void GetEast(const Point2D& point);
  inline void GetSouth(const Point2D& point);
  inline void GetWest(const Point2D& point);

  inline void SetNode(const Point2D& point, const Node& node);
  inline void SetNorth(const Point2D& point, const bool& state);
  inline void SetEast(const Point2D& point, const bool& state);
  inline void SetSouth(const Point2D& point, const bool& state);
  inline void SetWest(const Point2D& point, const bool& state);

  void Start() override;
  void OnGui(ImGuiContext *context) override;
  void OnDraw(SDL_Renderer* renderer) override;
  void Update(float deltaTime) override;

  void Clear();
};

#endif
