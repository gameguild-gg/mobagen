#include "PrimExample.h"
#include "../World.h"
#include "Random.h"

bool PrimExample::Step(World *w) {
  int sideOver2 = w->GetSize()/2;
  // bootstrap case
  if(!initialized){
    initialized = true;
    auto current = Point2D(-sideOver2, -sideOver2);
    toBeVisited.push_back(current);
    w->SetNodeColor(current, Color::Red.Dark());
    return true;
  }

  // end of the exploration
  if(toBeVisited.empty())
    return false;

  // visit a random element
  auto current = toBeVisited[Random::Range(0, toBeVisited.size()-1)];
  visited[current.y][current.x] = true;
  w->SetNodeColor(current, Color::Black);

  // wip
  auto visitables = getVisitables(w,current);
  return false;
}
void PrimExample::Clear(World *world) {
  toBeVisited.clear();
  initialized = false;
  auto sideOver2 = world->GetSize()/2;
  for(int i=-sideOver2;i<=sideOver2;i++){
    for(int j=-sideOver2;j<=sideOver2;j++){
      visited[i][j] = false;
    }
  }
}

std::vector<Point2D> PrimExample::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize()/2;
  std::vector<Point2D> visitables;

  // north
  if((abs(p.x)<=sideOver2 && abs(p.y-1)<=sideOver2) && // should be inside the board
      !visited[p.y-1][p.x] && // not visited yet
      w->GetNorth({p.x, p.y-1})) // has wall
    visitables.emplace_back(p.x, p.y-1);
  // east
  if((abs(p.x+1)<=sideOver2 && abs(p.y)<=sideOver2) && // should be inside the board
      !visited[p.y][p.x+1] && // not visited yet
      w->GetEast({p.x, p.y})) // has wall
    visitables.emplace_back(p.x+1, p.y);
  // south
  if((abs(p.x)<=sideOver2 && abs(p.y+1)<=sideOver2) && // should be inside the board
      !visited[p.y+1][p.x] && // not visited yet
      w->GetSouth({p.x, p.y})) // has wall
    visitables.emplace_back(p.x, p.y+1);
  // west
  if((abs(p.x-1)<=sideOver2 && abs(p.y)<=sideOver2) && // should be inside the board
      !visited[p.y][p.x-1] && // not visited yet
      w->GetWest({p.x, p.y})) // has wall
    visitables.emplace_back(p.x-1, p.y);

  return visitables;
}

std::vector<Point2D>
PrimExample::getVisitedNeighbors
    (World* w,const Point2D& p) {
  std::vector<Point2D> deltas = {{-1,0}, {0,-1}, {1,0}, {0,1}};
  auto sideOver2 = w->GetSize()/2;
  std::vector<Point2D> neighbors;

  for(auto delta: deltas){
    auto neigh = p+delta;
    if((abs(neigh.x)<=sideOver2 && abs(neigh.y)<=sideOver2) && // should be inside the board
        visited[neigh.y][neigh.x]) // visited
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
