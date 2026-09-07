#include "JohnConway.h"
#include "../fsm/Action.h"
#include "../fsm/AgentContext.h"
#include "../fsm/Condition.h"

// The four Conway rules as machine parts:
//   underpopulation / overpopulation -> conditions that leave Alive
//   reproduction                      -> condition that leaves Dead
//   survival is implicit: no transition firing means the stay actions run.

// begin solution

// end solution

JohnConway::JohnConway() {
  // begin solution

  // end solution
}

// Reference: https://playgameoflife.com/info
void JohnConway::Step(World& world) {
  // relevant functions:
  //   world.Height() and world.Width() to get the world dimensions,
  //   world.Get(), world.SetNext() to get the current state and set the next state (double buffering)
  // Build one context per cell and let the machine decide: conditions read the
  // current generation through the context, actions write the next one.
  // begin solution

  // end solution
}

int JohnConway::CountNeighbors(World& world, Point2D point) {
  // begin solution

  // end solution
  return 0;
}
