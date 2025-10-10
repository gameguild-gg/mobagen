#include "Agent.h"
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include "World.h"
using namespace std;
int Agent::heuristicManhattan(Point2D a, Point2D b) {
  return abs(a.x - b.x) + abs(a.y - b.y);
}
int heuristicBoard(Point2D& a, int worldSizeOver2) {
  return std::min(worldSizeOver2 - abs(a.x), worldSizeOver2 - abs(a.y));
}
std::vector<Point2D> Agent::getVisitableNeighbors(World* world, const Point2D& current, unordered_map<Point2D, Point2D>& visited) {
  auto sideOver2 = world->getWorldSideSize() / 2;
  std::vector<Point2D> visitables;
  std::vector<Point2D> changes = world->neighbors(current);
  for (auto i:changes) {
    Point2D hold = {i.x, i.y};
    if (!world->isValidPosition(hold) || !world->catcherCanMoveToPosition(hold) || world->getContent(hold)) {
      continue;
    }
    if (!visited.contains(hold)) {
      visitables.push_back(hold);
    }
  }
  return visitables;
}
struct Point2DPrioritized {
  Point2D point;
  int priority;
  int accCost;

  Point2DPrioritized(Point2D point, int priority, int acc): point(point), priority(priority), accCost(acc) {}

  // the < and > are reversed because we will give higher priority to the ones with less value
  bool operator<(const Point2DPrioritized &other) const {
    return priority > other.priority;
  }
};
std::vector<Point2D> Agent::generatePath(World* w) {
  unordered_map<Point2D, Point2D> cameFrom;  // to build the flowfield and build the path
  priority_queue<Point2DPrioritized> frontier;                   // to store next ones to visit
  auto worldSize = w->getWorldSideSize()/2;
  // bootstrap state
  Point2DPrioritized catPos = {w->getCat(), 0,0};
  frontier.push(catPos);
  Point2D borderExit = Point2D::INFINITE;  // if at the end of the loop we dont find a border, we have to return random points

  while (!frontier.empty()) {
    // get the current from frontier
    Point2DPrioritized current = frontier.top();

    frontier.pop();

    // getVisitableNeightbors(world, current, visited) returns a vector of neighbors that are not visited, not cat, not block, not in the queue
    vector<Point2D> neighs = getVisitableNeighbors(w, current.point, cameFrom);

    if (w->catWinsOnSpace(current.point)) {
      borderExit = current.point;
      break;
    }

    // iterate over the neighs:
    for (int i = 0;i<neighs.size();i++) {
      int newAcc = current.accCost + 1;
      if (!cameFrom.contains(neighs[i]) || newAcc < current.priority) {
        int newPriority = newAcc + heuristicBoard(neighs[i], worldSize); // todo: add distance to the bsoder
        cameFrom[neighs[i]] = current.point;
        Point2DPrioritized currentNeigh = {neighs[i], newPriority, newAcc};
        cameFrom.insert({currentNeigh.point, current.point});
        frontier.push(currentNeigh);
      }
    }
    // for every neighbor set the cameFrom
    // enqueue the neighbors to frontier and frontierset
    // do this up to find a visitable border and break the loop
  }
  if (borderExit != Point2D::INFINITE) {
    vector<Point2D> path;

    Point2D current = borderExit;

    while (current != catPos.point) {
      path.push_back(current);
      current = cameFrom.at(current);
    }

    return path;
  }
  // if the border is not infinity, build the path from border to the cat using the camefrom map
  // if there isnt a reachable border, just return empty vector
  // if your vector is filled from the border to the cat, the first element is the catcher move, and the last element is the cat move
  return vector<Point2D>();
}
