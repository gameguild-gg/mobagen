#include "../World.h"
#include "Random.h"
#include "RecursiveBacktrackerExample.h"
#include <climits>
bool RecursiveBacktrackerExample::Step(World* w) { 
  if (stack.size() == 0) 
   {
    stack.push_back(randomStartPoint(w));
   }
  Point2D current = stack[stack.size() - 1];
  std::vector<Point2D> visitables = getVisitables(w, current);
  if (visitables.size() == 0) 
  {
    w->SetNodeColor(current, Color::Black);
    stack.pop_back();
    if (stack.size() == 0) 
    {
      return false;
    }
  } else 
  {
    int index = 0; 
    if (visitables.size() > 1) 
    {
      index = Random::Range(0, visitables.size() - 1);
    }
    Point2D next = visitables[index];
    stack.push_back(next);

    Point2D difference =  next - current;

    if (difference.x == 1)
    {
      w->SetEast(current, false);
    } 
    else if (difference.x == -1) 
    {
      w->SetWest(current, false);
    } 
    else if (difference.y == -1) 
    {
      w->SetNorth(current, false);
    } 
    else if (difference.y == 1)
    {
      w->SetSouth(current, false);
    }
    w->SetNodeColor(current, Color::Green);
  }
  visited[current.x][current.y] = true;
  return true;
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
  if (!visited[p.x][p.y+1] && p.y < sideOver2 ) 
  {
    visitables.push_back(Point2D(p.x, p.y + 1));
  }

  if (!visited[p.x+1][p.y] && p.x < sideOver2) {
    visitables.push_back(Point2D(p.x + 1, p.y));
  }

  if (!visited[p.x][p.y - 1] && p.y > -sideOver2) {
    visitables.push_back(Point2D(p.x, p.y - 1));
  }
  
  if (!visited[p.x - 1][p.y] && p.x > -sideOver2) {
    visitables.push_back(Point2D(p.x - 1, p.y ));
  }
  return visitables;
}
