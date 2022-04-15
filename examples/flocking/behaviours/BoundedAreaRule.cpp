//#include "BoundedAreaRule.h"
//#include "../Boid.h"
//
//Vector2 BoundedAreaRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid) {
//    //Return a force proportional to the proximity of the boids with the bounds, and opposed to it
//    Vector2 force; //zero
//    Vector2 position = boid->getPosition();
//
//    float epsilon = 0.00001f;  //avoid div by zero
//
//    //Too close from min
//    if (position.x < desiredDistance)
//        force.x += desiredDistance / position.x; //car position.x = distance de 0 à x.
//
//        //Too close from max
//    else if (position.x > widthWindows - desiredDistance)
//    {
//        float d = position.x - widthWindows;
//        //if (d == 0) d = 0.0001; //avoid div by zero
//        force.x += desiredDistance / (d + epsilon);
//    }
//
//    //Too close from min
//    if (position.y < desiredDistance)
//        force.y += desiredDistance / position.y;
//
//    //Too close from max
//    else if (position.y > heightWindows - desiredDistance) {
//        float d = position.y - heightWindows;
//        //if (d == 0) d = 0.0001; //avoid div by zero
//        force.y += desiredDistance / (d + epsilon);
//    }
//
//    return force;
//}
//
//bool BoundedAreaRule::drawImguiRuleExtra() {
//    ImGui::SetCurrentContext(world->engine->imGuiContext);
//    bool valueHasChanged = false;
//
//    //We cap the max separation as the third of the min of the width.height
//    auto minHeightWidth = std::min(widthWindows, heightWindows);
//    int desired;
//    if (ImGui::SliderInt("Desired Distance From Borders",
//                         &desired,
//                         0.0f,
//                         (int)(minHeightWidth/3),
//                         "%i")) {
//        desiredDistance = (float)desired;
//        valueHasChanged = true;
//    }
//
//    return valueHasChanged;
//}
//
//void BoundedAreaRule::draw(const Boid& boid, SDL_Renderer* renderer) const {
//    FlockingRule::draw(boid, renderer);
//
//    //Draw a rectangle on the map
//    // todo: draw the rectangles
//
////    sf::RectangleShape bound(sf::Vector2f(widthWindows - 2 * desiredDistance, heightWindows - 2 * desiredDistance));
////    bound.setOutlineColor(debugColor);
////    bound.setOutlineThickness(1); //1 pixel outline
////    bound.setFillColor(sf::Color::Transparent); //Transparent inside, we just draw the box bound
////    bound.setPosition(desiredDistance, desiredDistance);
////
////    target.draw(bound, states);
//}