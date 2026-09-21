#include "PrimExample.h"
#include "../World.h"
#include "Random.h"




bool PrimExample::Step(World* w) {
  int sideOver2 = w->GetSize() / 2;
  if (toBeVisited.size() == 0) 
  {
    Point2D start = randomStartPoint(w);
    std::vector<Point2D> startNeighbors = getVisitables(w,start);
    for (Point2D nb : startNeighbors) 
    {
      toBeVisited.push_back(nb);
      w->SetNodeColor(nb, Color::Orange);
    }
    w->SetNodeColor(start, Color::Black);
    visited[start.x][start.y] = true;
  }
  else
  {
    int index = 0;
    if (toBeVisited.size() > 1) {
      index = Random::Range(0, toBeVisited.size()-1);
    }
    Point2D p = toBeVisited[index];
    toBeVisited.erase(toBeVisited.begin() + (index));
    std::vector<Point2D> startNeighbors = getVisitables(w, p);
    std::vector<Point2D> visitedNeighbors = getVisitedNeighbors(w, p);
    w->SetNodeColor(p, Color::Black);
    visited[p.x][p.y] = true;
    int vNindex = 0;
    
    if (visitedNeighbors.size() > 1) 
    {
      vNindex = Random::Range(0, visitedNeighbors.size() - 1);
    }
    Point2D neighbor = visitedNeighbors[vNindex];
    Point2D diff = neighbor - p;
    if (diff == deltas[0]) {
      w->SetNorth(p, false);
    } else if (diff == deltas[1]) {
      w->SetSouth(p, false);
    } else if (diff == deltas[2]) {
      w->SetWest(p, false);
    } else if (diff == deltas[3]) {
      w->SetEast(p, false);
    }
    for (Point2D nb : startNeighbors) {
      toBeVisited.push_back(nb);
      w->SetNodeColor(nb, Color::Orange);
    }
    if (toBeVisited.size() == 0) 
    {
      return false;
    }
  }
  

  return true;
}
void PrimExample::Clear(World* world) {
  toBeVisited.clear();
  visited.clear();
  initialized = false;
  auto sideOver2 = world->GetSize() / 2;

  for (int i = -sideOver2; i <= sideOver2; i++) {
    for (int j = -sideOver2; j <= sideOver2; j++) {
      visited[i][j] = false;
    }
  }
}

Point2D PrimExample::randomStartPoint(World* world) {
  auto sideOver2 = world->GetSize() / 2;

  // todo: change this if you want
  for (int y = -sideOver2; y <= sideOver2; y++)
    for (int x = -sideOver2; x <= sideOver2; x++)
      if (!visited[y][x]) return {x, y};
  return {INT_MAX, INT_MAX};
}

std::vector<Point2D> PrimExample::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> visitables;
  auto clearColor = Color32(169.0f / 255.0f, 169.0f / 255.0f, 169.0f / 255.0f, 1.0f);  // dark gray
  for (Point2D delta : deltas) {
    Point2D point = p + delta;
    if (!visited[point.x][point.y] && point.x >= -sideOver2 && point.x <= sideOver2 && point.y >= -sideOver2 && point.y <= sideOver2) {
      visitables.push_back(point);
    }
  }

  return visitables;
}

std::vector<Point2D> PrimExample::getVisitedNeighbors(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> neighbors;

  for (Point2D delta : deltas) 
  {
    Point2D point = p + delta;
    if (visited[point.x][point.y]) 
    {
      neighbors.push_back(point);
    }
  }

  return neighbors;
}
