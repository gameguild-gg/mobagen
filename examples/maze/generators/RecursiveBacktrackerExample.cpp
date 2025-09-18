#include "../World.h"
#include "Random.h"
#include "RecursiveBacktrackerExample.h"
#include <climits>
bool RecursiveBacktrackerExample::Step(World* w) {

  for (auto i = 0 ; i < stack.size() ; i++) {
    std::cout<<"["<<stack[i].x<<","<<stack[i].y<<"]";
  }
  std::cout<<std::endl;

  if (stack.empty()) {
    Point2D first = randomStartPoint(w);
    if (first.x == INT_MAX || first.y == INT_MAX) {
      return false;
    }
    visited[first.x][first.y] = true;
    stack.push_back(first);
    return true;
  }
  Point2D here = stack.back();
  w->SetNodeColor(here, Color::Purple);
  auto visitableNeighbors = getVisitables(w, here);
  if (!visitableNeighbors.empty()) {
  Point2D after = visitableNeighbors[Random::Range(0, visitableNeighbors.size() - 1)];
  if (after.x == here.x && after.y == here.y+1) {
    w->SetNorth(after, false);
    w->SetSouth(here, false);
  }
  else if (after.x == here.x+1 && after.y == here.y) {
  w->SetWest(after, false);
  w->SetEast(here, false);
  }else if (after.x == here.x && after.y == here.y-1) {
  w->SetSouth(after, false);
  w->SetNorth(here, false);
  } else if (after.x == here.x-1 && after.y == here.y) {
  w->SetWest(here, false);
  w->SetEast(after, false);
  }
  visited[after.x][after.y] = true;
  w->SetNodeColor(after, Color::Purple);
  stack.push_back(after);
  } else {
    stack.pop_back();
    w->SetNodeColor(here, Color::Black);
  }

  return !stack.empty();
}

void RecursiveBacktrackerExample::Clear(World* world) {
  visited.clear();
  stack.clear();
  auto sideOver2 = world->GetSize() / 2;

  for (int i = -sideOver2; i <= sideOver2; i++) {
    for (int j = -sideOver2; j <= sideOver2; j++) {
      visited[i][j] = false;
    }
  }
}

Point2D RecursiveBacktrackerExample::randomStartPoint(World* world) {
  auto sideOver2 = world->GetSize() / 2;

  // todo: change this if you want
  for (int y = -sideOver2; y <= sideOver2; y++)
    for (int x = -sideOver2; x <= sideOver2; x++)
      if (!visited[y][x]) return {x, y};
  return {INT_MAX, INT_MAX};
}

std::vector<Point2D> RecursiveBacktrackerExample::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> visitables;
  std::vector<Point2D> changes = {Point2D::UP, Point2D::DOWN, Point2D::LEFT, Point2D::RIGHT};
  for (auto i:changes) {
    Point2D hold = {p.x + i.x, p.y + i.y};
    if (hold.x < -sideOver2 || hold.x > sideOver2 || hold.y < -sideOver2 || hold.y > sideOver2) {
      continue;
    }
    if (!visited[hold.x][hold.y]) {
      visitables.push_back(hold);
    }
  }
  return visitables;
}
