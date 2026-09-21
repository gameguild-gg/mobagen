#include "HuntAndKillExample.h"
#include "../World.h"
#include "Random.h"
#include <climits>
bool HuntAndKillExample::Step(World* w) {
  if (stack.size() < 1) 
  {
    stack.push_back(randomStartPoint(w));
    w->SetNodeColor(stack[0], Color::Orange);
  }
  Point2D p = stack[stack.size() - 1];
  stack.pop_back();
  w->SetNodeColor(p, Color::Black);
  visited[p.x][p.y] = true;
  std::vector<Point2D> visitables = getVisitables(w, p);
  int index = 0;
  if (visitables.size() > 1) 
  {
    index = Random::Range(0, visitables.size() - 1);
  }
  else if (visitables.size() < 1)
  {
    Point2D huntedPoint = hunt(w);

    if (huntedPoint == Point2D{INT_MAX,INT_MAX}) 
    {
      return false;
    }

    stack.push_back(huntedPoint);
    int index2 = 0;
    std::vector<Point2D> visitedN = getVisitedNeighbors(w, huntedPoint);
    if (visitedN.size() > 1) 
    {
      index2 = Random::Range(0, visitedN.size() - 1);
    }
    Point2D neighbor = visitedN[index];
    Point2D diff = neighbor - huntedPoint;
    if (diff == deltas[0]) {
      w->SetNorth(huntedPoint, false);
    } else if (diff == deltas[1]) {
      w->SetSouth(huntedPoint, false);
    } else if (diff == deltas[2]) {
      w->SetWest(huntedPoint, false);
    } else if (diff == deltas[3]) {
      w->SetEast(huntedPoint, false);
    }

    return true;
  } 
  Point2D next = visitables[index];
  stack.push_back(next);
  w->SetNodeColor(next, Color::Orange);
  Point2D diff = next - p;
  if (diff == deltas[0]) {
    w->SetNorth(p, false);
  } else if (diff == deltas[1]) {
    w->SetSouth(p, false);
  } else if (diff == deltas[2]) {
    w->SetWest(p, false);
  } else if (diff == deltas[3]) {
    w->SetEast(p, false);
  }
  return true;
}
void HuntAndKillExample::Clear(World* world) {
  visited.clear();
  stack.clear();
  auto sideOver2 = world->GetSize() / 2;

  for (int i = -sideOver2; i <= sideOver2; i++) {
    for (int j = -sideOver2; j <= sideOver2; j++) {
      visited[i][j] = false;
    }
  }
}
Point2D HuntAndKillExample::randomStartPoint(World* world) {
  // Todo: improve this if you want
  auto sideOver2 = world->GetSize() / 2;

  for (int y = -sideOver2; y <= sideOver2; y++)
    for (int x = -sideOver2; x <= sideOver2; x++)
      if (!visited[y][x]) return {x, y};
  return {INT_MAX, INT_MAX};
}

std::vector<Point2D> HuntAndKillExample::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> visitables;

  for (Point2D delta : deltas) 
  {
    Point2D point = p + delta;
    if (!visited[point.x][point.y] && point.x >= -sideOver2 && point.x <= sideOver2 && point.y >= -sideOver2 && point.y <= sideOver2) 
    {
      visitables.push_back(point);
    }
  }

  return visitables;
}
std::vector<Point2D> HuntAndKillExample::getVisitedNeighbors(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> neighbors;

   for (Point2D delta : deltas) {
    Point2D point = p + delta;
    if (visited[point.x][point.y]) {
      neighbors.push_back(point);
    }
  }

  return neighbors;
}

Point2D HuntAndKillExample::hunt(World* w) 
{
  auto sideOver2 = w->GetSize() / 2;

  for (int y = -sideOver2; y <= sideOver2; y++)
    for (int x = -sideOver2; x <= sideOver2; x++)
      if (!visited[y][x] && getVisitedNeighbors(w,{y,x}).size() > 0) return {y, x};
  return {INT_MAX, INT_MAX};

}
