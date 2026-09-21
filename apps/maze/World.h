#ifndef MOBAGEN_WORLD_H
#define MOBAGEN_WORLD_H

#include "imgui.h"
#include "math/ColorT.h"
#include "MazeGeneratorBase.h"
#include "Node.h"
#include "math/Point2D.h"
#include <cassert>
#include <vector>

class World {
private:
  int width;
  int height;

  std::vector<MazeGeneratorBase*> generators;
  int generatorId = 0;
  bool isSimulating = false;
  float timeBetweenAITicks = 0.0;
  float timeForNextTick = 0;
  int64_t moveDuration = 0;
  int64_t totalTime = 0;

  // ._
  // |
  // even indexes are top elements;
  // odd indexes are left elements;
  std::vector<bool> data;
  // the boxes colors
  std::vector<Color32> colors;
  // convert a point into the index of the left vertex of the node.
  // points are in grid units: (0, 0) is the top-left cell, x grows right,
  // y grows down; valid while 0 <= x < width and 0 <= y < height.
  inline int Point2DtoIndex(const Point2D& point) {
    // todo: test. unstable interface
    assert(0 <= point.x && point.x < width && 0 <= point.y && point.y < height);
    return point.y * (width + 1) * 2 + point.x * 2;
  }

public:
  ~World();
  explicit World(int size = 11);

  Node GetNode(const Point2D& point);
  bool GetNorth(const Point2D& point);
  bool GetEast(const Point2D& point);
  bool GetSouth(const Point2D& point);
  bool GetWest(const Point2D& point);

  void SetNode(const Point2D& point, const Node& node);
  void SetNorth(const Point2D& point, const bool& state);
  void SetEast(const Point2D& point, const bool& state);
  void SetSouth(const Point2D& point, const bool& state);
  void SetWest(const Point2D& point, const bool& state);

  // All points are in grid units: (0, 0) is the top-left cell, x grows right,
  // y grows down; valid while 0 <= x < width and 0 <= y < height.

  void Start();
  void OnGui();
  void OnDraw();
  void Update(float deltaTime);

  void Clear();

  void SetNodeColor(const Point2D& node, const Color32& color);
  Color32 GetNodeColor(const Point2D& node);

  int GetWidth() const;
  int GetHeight() const;

  // square grids (interactive app)
  void Resize(int size);
  // rectangular grids (formal tests): width x height
  void Resize(int width, int height);

private:
  void step();
};

#endif
