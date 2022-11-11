#include "HuntAndKillExample.h"
#include "../World.h"
#include "Random.h"
#include <climits>
bool HuntAndKillExample::Step(World* w) {
  // check if we need to find a new starting point
  if(stack.empty()) {
    auto point = randomStartPoint(w);
    if(point.x==INT_MAX && point.y==INT_MAX)
      return false; // no empty space no fill
    stack.push_back(point);

    // not the bootstrap state
    if(point.x!=-w->GetSize()/2 || point.y!=-w->GetSize()/2) {
      // remove wall in the first node if it it starts to go deep again
      auto visitedNeighbors = getVisitedNeighbors(w, point);
      if (visitedNeighbors.empty())
        return false;  // this should never happen. if we are in this state, the code is wrong
      auto next =
          visitedNeighbors[Random::Range(0, visitedNeighbors.size() - 1)];
      auto delta = next - point;
      // remove walls
      if (delta.y == -1)  // north
        w->SetNorth(point, false);
      else if (delta.x == 1)  // east
        w->SetEast(point, false);
      else if (delta.y == 1)  // south
        w->SetSouth(point, false);
      else if (delta.x == -1)  // west
        w->SetWest(point, false);
      else
        return false;  // this should never happen. if we are in this state, the code is wrong
    }
  }

  // visit the current element
  auto current = stack.back();
  visited[current.y][current.x] = true;
  w->SetNodeColor(current, Color::Red.Dark());

  // check if we should go deeper
  std::vector<Point2D> visitables = getVisitables(w, current);

  // if we should not go deep, pop one element from the stack
  if(visitables.empty()) {
    for(auto v: stack)
      w->SetNodeColor(v, Color::Black);
    stack.clear();
    return true;
  }
  else { // go deeper
    auto r = Random::Range(0, visitables.size()-1);
    auto next = visitables[r];
    w->SetNodeColor(next, Color::Green);
    stack.push_back(next);
    auto delta = next - current;
    // remove walls
    if(delta.y==-1) // north
      w->SetNorth(current, false);
    else if(delta.x==1) // east
      w->SetEast(current, false);
    else if(delta.y==1) // south
      w->SetSouth(current, false);
    else if(delta.x==-1) // west
      w->SetWest(current, false);
    return true;
  }
}
void HuntAndKillExample::Clear(World* world) {
  visited.clear();
  stack.clear();
  auto sideOver2 = world->GetSize()/2;

  for(int i=-sideOver2;i<=sideOver2;i++){
    for(int j=-sideOver2;j<=sideOver2;j++){
      visited[i][j] = false;
    }
  }
}
Point2D HuntAndKillExample::randomStartPoint(World* world) {
  auto sideOver2 = world->GetSize()/2;

  for(int y=-sideOver2; y<=sideOver2; y++)
    for(int x=-sideOver2; x<=sideOver2; x++)
      if(!visited[y][x])
        return {x,y};
  return {INT_MAX, INT_MAX};
}

std::vector<Point2D> HuntAndKillExample::getVisitables(World* w, const Point2D& p) {
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
    HuntAndKillExample::getVisitedNeighbors
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
