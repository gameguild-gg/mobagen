#include "../World.h"
#include "../SeededRandom.h"
#include "RecursiveBacktrackerExample.h"
#include <climits>

// Recursive backtracker, in grid units: (0, 0) is the top-left cell, x grows
// right, y grows down — the same units as the World API. The caller seeds
// SeededRandom before the first Step; every decision consumes the seed in
// order, so the maze is deterministic.
//
// Procedure per Step, on the cell at the top of the path stack:
//   1. mark it visited;
//   2. list its visitable (unvisited) neighbors in clockwise order starting
//      from the top: UP, RIGHT, DOWN, LEFT (getVisitables does this);
//   3. none        -> dead end: pop the stack (backtrack). Empty stack = done;
//   4. exactly one -> move to it, do not consume a random number;
//   5. two or more -> consume SeededRandom::next() and pick
//      next() % visitableCount;
//   6. moving opens the wall between the two cells
//      (World::SetNorth/SetEast/SetSouth/SetWest with false).

void RecursiveBacktrackerExample::Clear(World* world) {
  // todo: reset the walk
  // hint:
  //   clear visited and the path stack, then start the walk at the
  //   top-left cell: stack.push_back({0, 0})
  // begin solution

  visited.clear();
  stack.clear();
  stack.push_back({0, 0});

  // end solution
}

bool RecursiveBacktrackerExample::Step(World* w) {
  // todo: implement one iteration of the recursive backtracker
  // hint:
  //   empty stack  -> the maze is done, return false
  //   otherwise, on the cell at the top of the stack:
  //   1. mark it visited;
  //   2. list its visitable neighbors with getVisitables
  //      (already in clockwise order: UP, RIGHT, DOWN, LEFT);
  //   3. none        -> dead end: pop the stack (backtrack);
  //   4. exactly one -> move to it, do not consume a random number;
  //   5. two or more -> consume SeededRandom::next() and pick
  //      next() % visitables.size();
  //   moving = opening the wall between the two cells:
  //     UP    -> w->SetNorth(current, false)
  //     RIGHT -> w->SetEast(current, false)
  //     DOWN  -> w->SetSouth(current, false)
  //     LEFT  -> w->SetWest(current, false)
  //   return true while there is still work (stack not empty after the move)
  // begin solution

  if (stack.empty()) {
    return false;
  }

  Point2D currentPoint = stack.back();
  visited[currentPoint.y][currentPoint.x] = true;

  std::vector<Point2D> neighbors = getVisitables(w, currentPoint);
  if (neighbors.empty()) {
    stack.pop_back();
    return true;
  }
  else if (neighbors.size() == 1) {
    visited[neighbors.back().y][neighbors.back().x] = true;
    stack.push_back(neighbors.back());
    if (neighbors.back().x > currentPoint.x) {
      w->SetEast(currentPoint, false);
      return true;
    }
    else if (neighbors.back().x < currentPoint.x) {
      w->SetWest(currentPoint, false);
      return true;
    }
    else if (neighbors.back().y > currentPoint.y) {
      w->SetSouth(currentPoint, false);
      return true;
    }
    else if (neighbors.back().y < currentPoint.y) {
      w->SetNorth(currentPoint, false);
      return true;
    }
  }
  else if (neighbors.size() > 1) {
    Point2D neighbor = neighbors[SeededRandom::next() % neighbors.size()];
    visited[neighbor.y][neighbor.x] = true;
    stack.push_back(neighbor);
    if (neighbor.x > currentPoint.x) {
      w->SetEast(currentPoint, false);
      return true;
    }
    else if (neighbor.x < currentPoint.x) {
      w->SetWest(currentPoint, false);
      return true;
    }
    else if (neighbor.y > currentPoint.y) {
      w->SetSouth(currentPoint, false);
      return true;
    }
    else if (neighbor.y < currentPoint.y) {
      w->SetNorth(currentPoint, false);
      return true;
    }
  }

  // end solution
  return false;
}

std::vector<Point2D> RecursiveBacktrackerExample::getVisitables(World* w, const Point2D& point) {
  // todo: list the unvisited neighbors of point, in clockwise order
  // hint:
  //   candidates in order: UP {x, y-1}, RIGHT {x+1, y}, DOWN {x, y+1}, LEFT {x-1, y}
  //   keep a candidate only if it is inside the grid
  //   (0 <= x < w->GetWidth(), 0 <= y < w->GetHeight()) and not visited
  // begin solution

  std::vector<Point2D> visitable;
  if (point.y - 1 >= 0 && !visited[point.y - 1][point.x]) {
    visitable.push_back(Point2D(point.x, point.y - 1));
  }
  if (point.x + 1 < w->GetWidth() && !visited[point.y][point.x + 1]) {
    visitable.push_back(Point2D(point.x + 1, point.y));
  }
  if (point.y + 1 < w->GetHeight() && !visited[point.y + 1][point.x]) {
    visitable.push_back(Point2D(point.x, point.y + 1));
  }
  if (point.x - 1 >= 0 && !visited[point.y][point.x - 1]) {
    visitable.push_back(Point2D(point.x - 1, point.y));
  }

  // end solution
  return visitable;
}
