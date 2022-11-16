#include "PrimExample.h"
#include "../World.h"
#include "Random.h"

bool PrimExample::Step(World *w) {
  int sideOver2 = w->GetSize()/2;

  auto visitedColor = Color::Black;
  auto queuedColor = Color::DarkRed;
  auto clearColor = Color::DarkGray;

  if(!initialized){
    initialized = true;
    auto current = Point2D(-sideOver2, -sideOver2);
    toBeVisited.push_back(current);
    w->SetNodeColor(current, queuedColor);
    return true;
  }

  // end of the exploration
  if(toBeVisited.empty())
    return false;

  // visit a random element
  auto randomId = Random::Range(0, toBeVisited.size()-1);
  auto current = toBeVisited[randomId];
  toBeVisited.erase(toBeVisited.begin() + randomId);
  w->SetNodeColor(current, visitedColor);

  // add visitables of the current element to BeVisited list
  auto visitables = getVisitables(w, current);
  for(auto v: visitables){
    toBeVisited.push_back(v);
    w->SetNodeColor(v,queuedColor);
  }

  // remove the wall between the current and a random visited neighbor
  auto visited = getVisitedNeighbors(w,current);
  if(!visited.empty()){
    auto next =
        visited[Random::Range(0, visited.size() - 1)];
    auto delta = next - current;
    // remove walls
    if (delta.y == -1)  // north
      w->SetNorth(current, false);
    else if (delta.x == 1)  // east
      w->SetEast(current, false);
    else if (delta.y == 1)  // south
      w->SetSouth(current, false);
    else if (delta.x == -1)  // west
      w->SetWest(current, false);
  }
  return true;
}
void PrimExample::Clear(World *world) {
  toBeVisited.clear();
  initialized = false;
}

std::vector<Point2D> PrimExample::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize()/2;
  std::vector<Point2D> visitables;

  // north
  if((abs(p.x)<=sideOver2 && abs(p.y-1)<=sideOver2) && // should be inside the board
      w->GetNodeColor(p+Point2D::UP) == Color::DarkGray) // not visited yet
    visitables.emplace_back(p.x, p.y-1);
  // east
  if((abs(p.x+1)<=sideOver2 && abs(p.y)<=sideOver2) && // should be inside the board
      w->GetNodeColor(p+Point2D::RIGHT) == Color::DarkGray) // not visited yet
    visitables.emplace_back(p.x+1, p.y);
  // south
  if((abs(p.x)<=sideOver2 && abs(p.y+1)<=sideOver2) && // should be inside the board
      w->GetNodeColor(p+Point2D::DOWN) == Color::DarkGray) // not visited yet
    visitables.emplace_back(p.x, p.y+1);
  // west
  if((abs(p.x-1)<=sideOver2 && abs(p.y)<=sideOver2) && // should be inside the board
      w->GetNodeColor(p+Point2D::LEFT) == Color::DarkGray) // not visited yet
    visitables.emplace_back(p.x-1, p.y);

  return visitables;
}

std::vector<Point2D>
PrimExample::getVisitedNeighbors
    (World* w,const Point2D& p) {
  std::vector<Point2D> deltas = {Point2D::UP, Point2D::DOWN, Point2D::LEFT, Point2D::RIGHT};
  auto sideOver2 = w->GetSize()/2;
  std::vector<Point2D> neighbors;

  for(auto delta: deltas){
    auto neigh = p+delta;
    if((abs(neigh.x)<=sideOver2 && abs(neigh.y)<=sideOver2) && // should be inside the board
        w->GetNodeColor(neigh) == Color::Black) // visited
    {
      bool wall;
      if(delta.y==-1) // north
        wall = w->GetNorth(p);
      else if(delta.x==1) // east
        wall = w->GetEast(p);
      else if(delta.y==1) // south
        wall = w->GetSouth(p);
      else if(delta.x==-1) // west
        wall = w->GetWest(p);
      else
        wall = false; // this should never happens

      if(wall) // it should have wall
        neighbors.emplace_back(neigh);
    }
  }

  return neighbors;
}
