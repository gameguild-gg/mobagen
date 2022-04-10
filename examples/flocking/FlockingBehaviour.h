#pragma once

#include "Vector2.h"
#include "FlockingAgent.h"
#include "Polygon.h"
#include "Flock.h"

class FlockingBehaviour {
    // all behaviours needs to implement the move logic
    Vector2 virtual CalculateMove(FlockingAgent agent, std::vector<Polygon> context, Flock flock)=0;
};
