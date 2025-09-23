#include "HuntAndKillExample.h"
#include "../World.h"
#include "Random.h"
#include <climits>
#include <stack>
#include <variant>
bool HuntAndKillExample::Step(World* w) {
if (stack.empty()) {
  Point2D startPoint = randomStartPoint(w);
  stack.push_back(startPoint);
}
Point2D current = stack.back();
w->SetNodeColor(current, Color::Black);
visited[current.x][current.y] = true;
auto visitableNeighbors = getVisitables(w, current);
  if (!visitableNeighbors.empty()) {
  //if we have visitable neighbors, randomly go to one

    int dir = randoNum() % visitableNeighbors.size();

    do {
    dir = randoNum() % visitableNeighbors.size();
    }while (visited[visitableNeighbors[dir].x][visitableNeighbors[dir].y] != false);

    if (visitableNeighbors[dir].x == current.x && visitableNeighbors[dir].y == current.y+1) {
      w->SetNorth(visitableNeighbors[dir], false);
      w->SetSouth(current, false);
    }
    else if (visitableNeighbors[dir].x == current.x+1 && visitableNeighbors[dir].y == current.y) {
      w->SetWest(visitableNeighbors[dir], false);
      w->SetEast(current, false);
    }else if (visitableNeighbors[dir].x == current.x && visitableNeighbors[dir].y == current.y-1) {
      w->SetSouth(visitableNeighbors[dir], false);
      w->SetNorth(current, false);
    } else if (visitableNeighbors[dir].x == current.x-1 && visitableNeighbors[dir].y == current.y) {
      w->SetWest(current, false);
      w->SetEast(visitableNeighbors[dir], false);
    }
    //visited[visitableNeighbors[dir].x][visitableNeighbors[dir].y] = true;
    stack.push_back(visitableNeighbors[dir]);
  }
  else {
      stack.pop_back();
      for (int i = 0;i< stack.size(); ++i) {
        if (!getVisitables(w, stack[i]).empty()) {
          stack.push_back(stack[i]);
          break;
        }
      }
    }
  return !stack.empty();
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
std::vector<Point2D> HuntAndKillExample::getVisitedNeighbors(World* w, const Point2D& p) {
  std::vector<Point2D> deltas = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
  auto sideOver2 = w->GetSize() / 2;
  std::vector<Point2D> neighbors;
  for (auto i : deltas) {
    Point2D hold = {p.x + i.x, p.y + i.y};
    if (hold.x < -sideOver2 || hold.x > sideOver2 || hold.y < -sideOver2 || hold.y > sideOver2) {
      continue;
    }
    if (visited[hold.x][hold.y]) {
      neighbors.push_back(hold);
    }
  }

  return neighbors;
}
int HuntAndKillExample::randoNum() {
if (bindex == 16) {
  bindex = 0;
}
  bindex++;
  return beep[bindex];
}
