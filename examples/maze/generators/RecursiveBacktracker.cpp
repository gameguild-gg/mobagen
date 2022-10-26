#include "RecursiveBacktracker.h"
#include "../World.h"
#include "Random.h"
bool RecursiveBacktracker::Step(World* w) {
  auto sideOver2 = w->GetSize()/2;

  // check if we need to find a new starting point
  if(stack.empty()) {
    auto point = randomStartPoint(w);
    if(point.x==INT_MAX && point.y==INT_MAX)
      return false; // no empty space no fill
    stack.push_back(point);
    w->SetNodeColor(point, Color::Red.Dark());
  }

  // visit the current element
  visited[stack.back().y][stack.back().x] = true;

  // check if we should go deeper
  std::vector<Point2D> visitables = getVisitables(w, stack.back());

  // if we should not go deep, pop one element from the stack
  if(visitables.size()==0) {
    auto tail = stack.back();
    stack.pop_back();
    w->SetNodeColor(tail, Color::Black);
    return true;
  }
  else {
    auto r = Random::Range(0, visitables.size()-1);
    w->SetNodeColor(visitables[r], Color::Red.Dark());
    stack.push_back(visitables[r]);
    return true;
  }

  return false;
}
void RecursiveBacktracker::Clear(World* world) {
  visited.clear();
  stack.clear();
  auto sideOver2 = world->GetSize()/2;

  for(int i=-sideOver2;i<=sideOver2;i++){
    for(int j=-sideOver2;j<=sideOver2;j++){
      visited[i][j] = false;
    }
  }
}
Point2D RecursiveBacktracker::randomStartPoint(World* world) {
  auto sideOver2 = world->GetSize()/2;

  for(int y=-sideOver2; y<=sideOver2; y++)
    for(int x=-sideOver2; x<=sideOver2; x++)
      if(!visited[y][x])
        return {x,y};
  return {INT_MAX, INT_MAX};
}

std::vector<Point2D> RecursiveBacktracker::getVisitables(World* w, const Point2D& p) {
  auto sideOver2 = w->GetSize()/2;
  std::vector<Point2D> visitables;

  // todo: improve this
  // north
  if((abs(p.x)<=sideOver2 && abs(p.y-1)<=sideOver2) && // should be inside the board
    !visited[p.y-1][p.x] && // not visited yet
    w->GetNorth({p.x, p.y-1})) // has wall
      visitables.emplace_back(p.x, p.y-1);
  // east
  if((abs(p.x+1)<=sideOver2 && abs(p.y)<=sideOver2) && // should be inside the board
      !visited[p.y][p.x+1] && // not visited yet
      w->GetNorth({p.x+1, p.y})) // has wall
    visitables.emplace_back(p.x+1, p.y);
  // south
  if((abs(p.x)<=sideOver2 && abs(p.y+1)<=sideOver2) && // should be inside the board
      !visited[p.y+1][p.x] && // not visited yet
      w->GetNorth({p.x, p.y+1})) // has wall
    visitables.emplace_back(p.x, p.y+1);
  // west
  if((abs(p.x-1)<=sideOver2 && abs(p.y)<=sideOver2) && // should be inside the board
      !visited[p.y][p.x-1] && // not visited yet
      w->GetNorth({p.x-1, p.y})) // has wall
    visitables.emplace_back(p.x-1, p.y);

  return visitables;
}
