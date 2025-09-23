#include "PrimExample.h"
#include "../World.h"
#include "Random.h"

bool PrimExample::Step(World* w) {
  int sideOver2 = w->GetSize() / 2;

  // todo: code this

  return true;
}
void PrimExample::Clear(World* world) {
  toBeVisited.clear();
  initialized = false;
}

std::vector<Point2D> PrimExample::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> visitables;
  auto clearColor = Color::DarkGray;
  /*std::vector<Point2D> changes = {Point2D::UP, Point2D::DOWN, Point2D::LEFT, Point2D::RIGHT};
  for (auto i:changes) {
    Point2D hold = {p.x + i.x, p.y + i.y};
    if (hold.x < -sideOver2 || hold.x > sideOver2 || hold.y < -sideOver2 || hold.y > sideOver2) {
      continue;
    }
    if (!visited[hold.x][hold.y]) {
      visitables.push_back(hold);
    }
  }
  return visitables; */
}

std::vector<Point2D> PrimExample::getVisitedNeighbors(World* w, const Point2D& p) {
  std::vector<Point2D> deltas = {Point2D::UP, Point2D::DOWN, Point2D::LEFT, Point2D::RIGHT};
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> neighbors;

  // todo: code this

  return neighbors;
}
