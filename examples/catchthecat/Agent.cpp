#include "Agent.h"
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include "World.h"
using namespace std;
std::vector<Point2D> Agent::getVisitableNeighbors(World* world, const Point2D& current, unordered_set<Point2D>& visited) {
  auto sideOver2 = world->getWorldSideSize() / 2;
  std::vector<Point2D> visitables;
  std::vector<Point2D> changes = world->neighbors(current);
  for (auto i:changes) {
    Point2D hold = {current.x + i.x, current.y + i.y};
    if (world->isValidPosition(hold)) {
      continue;
    }
    if (!visited.contains(hold)) {
      visitables.push_back(hold);
    }
  }
  return visitables;
}
std::vector<Point2D> Agent::generatePath(World* w) {
  unordered_map<Point2D, Point2D> cameFrom;  // to build the flowfield and build the path
  queue<Point2D> frontier;                   // to store next ones to visit
  unordered_set<Point2D> frontierSet;        // OPTIMIZATION to check faster if a point is in the queue
  unordered_set<Point2D> visited;      // use .at() to get data, if the element dont exist [] will give you wrong results

  // bootstrap state
  auto catPos = w->getCat();
  frontier.push(catPos);
  frontierSet.insert(catPos);
  Point2D borderExit = Point2D::INFINITE;  // if at the end of the loop we dont find a border, we have to return random points

  while (!frontier.empty()) {
    // get the current from frontier
    Point2D current = frontier.front();
    frontier.pop();
    frontierSet.erase(current);
    // remove the current from frontierset
    // mark current as visited
    visited.insert(current);
    // getVisitableNeightbors(world, current, visited) returns a vector of neighbors that are not visited, not cat, not block, not in the queue
    vector<Point2D> neighs = getVisitableNeighbors(w, current, visited);
    // iterate over the neighs:
    for (int i = 0;i<neighs.size();i++) {
      if (!visited.contains(neighs[i])) {
      cameFrom.insert({neighs[i], current});
      frontier.push(neighs[i]);
      frontierSet.insert(neighs[i]);
      }
    }
    // for every neighbor set the cameFrom
    // enqueue the neighbors to frontier and frontierset
    // do this up to find a visitable border and break the loop
  if (w->catWinsOnSpace(current)) {
    break;
  }
  }
  
  // if the border is not infinity, build the path from border to the cat using the camefrom map
  // if there isnt a reachable border, just return empty vector
  // if your vector is filled from the border to the cat, the first element is the catcher move, and the last element is the cat move
  return vector<Point2D>();
}
