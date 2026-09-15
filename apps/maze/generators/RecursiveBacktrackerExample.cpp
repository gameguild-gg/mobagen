#include "../World.h"
#include "../SeededRandom.h"
#include "RecursiveBacktrackerExample.h"
#include <climits>

// Recursive backtracker, in FORMAL units: (0, 0) is the top-left cell, x grows
// right, y grows down. The caller seeds SeededRandom before the first Step;
// every decision consumes the seed in order, so the maze is deterministic.
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
  //   top-left cell in formal units: stack.push_back({0, 0})
  // begin solution

  // end solution
}

bool RecursiveBacktrackerExample::Step(World* w) {
  // todo: implement one iteration of the recursive backtracker
  // hint:
  //   empty stack  -> the maze is done, return false
  //   otherwise, on the cell at the top of the stack (formal units):
  //   1. mark it visited;
  //   2. list its visitable neighbors with getVisitables
  //      (already in clockwise order: UP, RIGHT, DOWN, LEFT);
  //   3. none        -> dead end: pop the stack (backtrack);
  //   4. exactly one -> move to it, do not consume a random number;
  //   5. two or more -> consume SeededRandom::next() and pick
  //      next() % visitables.size();
  //   moving = opening the wall between the two cells, through the
  //   World coordinate translation:
  //     Point2D worldCurrent = w->ToWorldCoords(current);
  //     UP    -> w->SetNorth(worldCurrent, false)
  //     RIGHT -> w->SetEast(worldCurrent, false)
  //     DOWN  -> w->SetSouth(worldCurrent, false)
  //     LEFT  -> w->SetWest(worldCurrent, false)
  //   return true while there is still work (stack not empty after the move)
  // begin solution

  // end solution
  return false;
}

std::vector<Point2D> RecursiveBacktrackerExample::getVisitables(World* w, const Point2D& formalPoint) {
  // todo: list the unvisited neighbors of formalPoint, in clockwise order
  // hint:
  //   candidates in order: UP {x, y-1}, RIGHT {x+1, y}, DOWN {x, y+1}, LEFT {x-1, y}
  //   keep a candidate only if it is inside the grid
  //   (0 <= x < w->GetWidth(), 0 <= y < w->GetHeight()) and not visited
  // begin solution

  // end solution
  return {};
}
