#include "BoundedAreaRule.h"
#include "../gameobjects/Boid.h"
#include "../gameobjects/World.h"

Vector2 BoundedAreaRule::computeForce(const std::vector<Boid*>& neighborhood, Boid* boid) {
    //Return a force proportional to the proximity of the boids with the bounds, and opposed to it
    Vector2 force; //zero
    Vector2 position = boid->getPosition();

    auto size = this->world->engine->window->size();
    auto widthWindows = size.x;
    auto heightWindows = size.y;

    float epsilon = 0.00001f;  //avoid div by zero

    //Too close from min
    if (position.x < desiredDistance)
        force.x += desiredDistance / position.x; //car position.x = distance de 0 à x.

        //Too close from max
    else if (position.x > widthWindows - desiredDistance)
    {
        float d = position.x - widthWindows;
        //if (d == 0) d = 0.0001; //avoid div by zero
        force.x += desiredDistance / (d + epsilon);
    }

    //Too close from min
    if (position.y < desiredDistance)
        force.y += desiredDistance / position.y;

    //Too close from max
    else if (position.y > heightWindows - desiredDistance) {
        float d = position.y - heightWindows;
        //if (d == 0) d = 0.0001; //avoid div by zero
        force.y += desiredDistance / (d + epsilon);
    }

    return force;
}

bool BoundedAreaRule::drawImguiRuleExtra() {
    ImGui::SetCurrentContext(world->engine->imGuiContext);
    auto size = this->world->engine->window->size();
    auto widthWindows = size.x;
    auto heightWindows = size.y;
    bool valueHasChanged = false;

    //We cap the max separation as the third of the min of the width.height
    auto minHeightWidth = std::min(widthWindows, heightWindows);

    if (ImGui::SliderInt("Desired Distance From Borders",
                         &desiredDistance,
                         0.0f,
                         (int)(minHeightWidth/3),
                         "%i")) {
        valueHasChanged = true;
    }

    return valueHasChanged;
}

void BoundedAreaRule::draw(const Boid& boid, SDL_Renderer* renderer) const {
    FlockingRule::draw(boid, renderer);
    auto size = this->world->engine->window->size();
    auto dist = (float)desiredDistance;

    // Draw a rectangle on the map
    Polygon::DrawLine(renderer, Vector2(dist,dist), Vector2(size.x - dist,dist), Vector3::Gray()); // TOP
    Polygon::DrawLine(renderer, Vector2(size.x - dist,dist), Vector2(size.x - dist,size.y - dist), Vector3::Gray()); // RIGHT
    Polygon::DrawLine(renderer, Vector2(size.x - dist,size.y - dist), Vector2(dist,size.y - dist), Vector3::Gray()); // Bottom
    Polygon::DrawLine(renderer, Vector2(dist,size.y - dist), Vector2(dist,dist), Vector3::Gray()); // LEFT
}