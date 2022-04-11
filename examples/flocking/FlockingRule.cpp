#include "FlockingRule.h"

#include "Utils.h"
#include "Boid.h"
#include "imgui.h"
#include "ImGuiExtra.h"

using namespace utils;

FlockingRule::FlockingRule(const FlockingRule& toCopy)
{
    weight = toCopy.weight;
    debugColor = toCopy.debugColor;
    force = toCopy.force;
    isEnabled = toCopy.isEnabled;
}

sf::Vector2f FlockingRule::computeWeightedForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{
    //the computed force is cached in a var

    if (isEnabled)
    {
        force = getBaseWeightMultiplier() * weight * computeForce(neighbordhood, boid);
    }
    else
    {
        //If the rule is not enabled, return vector zero.
        force = sf::Vector2f();
    }

    return force;
}

bool FlockingRule::drawImguiRule()
{
    bool valueHasChanged = false;


    ImGui::SetNextItemOpen(isEnabled, ImGuiCond_Once); //Opened by default if rule active
    if (ImGui::TreeNode(getRuleName()))
    {
        ImguiTooltip(getRuleExplanation());

        if (ImGui::Checkbox("Enabled", &isEnabled))
        {
            valueHasChanged = true;
        }

        if (isEnabled)
        {
            if (ImGui::DragFloat("Weight##", &weight, 0.025f))
            {
                valueHasChanged = true;
            }

            ImGui::SameLine(); HelpMarker("Drag to change the weight's value or CTRL+Click to input a new value.");

            //Additional settings rule-dependant
            if (drawImguiRuleExtra())
            {
                valueHasChanged = true;
            }
        }



        ImGui::TreePop();
    }
    else {
        ImguiTooltip(getRuleExplanation());
    }


    return valueHasChanged;
}

void FlockingRule::draw(const Boid& boid, sf::RenderTarget& target, sf::RenderStates states) const
{
    //Scaled by 1.5f to see it more easily on screen.
    graphics::drawVector(target, states, boid.getPosition(), force * 1.5f, debugColor);
}

sf::Vector2f CohesionRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{

    sf::Vector2f cohesionForce;

    if (neighbordhood.size() > 0)
    {

        sf::Vector2f centerOfMass;

        for (auto const& flockmate : neighbordhood)
        {
            centerOfMass += flockmate->getPosition();
        }

        centerOfMass /= static_cast<float>(neighbordhood.size());

        //Get direction toward center of mass
        sf::Vector2f towardCenter = centerOfMass - boid->getPosition();

        cohesionForce = utils::vector2::normalized(towardCenter);
    }

    return cohesionForce;
}

sf::Vector2f SeparationRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{

    //Try to avoid boids too close
    sf::Vector2f separatingForce;

    float desiredDistance = desiredMinimalDistance;

    if (neighbordhood.size() > 0)
    {
        sf::Vector2f position = boid->getPosition();
        int countCloseFlockmates = 0;

        for (auto const& flockmate : neighbordhood)
        {
            float distance = vector2::getDistance(position, flockmate->getPosition());

            if (distance < desiredDistance && distance > 0)
            {
                sf::Vector2f opposedDirection = vector2::normalized(position - flockmate->getPosition());

                //Each neighbor has an influence proportional to its distance
                separatingForce += opposedDirection / distance;
                countCloseFlockmates++;
            }
        }

        if (countCloseFlockmates > 0) {
            //We doesn't divide by neighboor counts, but too clost neighbor counts.
            separatingForce /= static_cast<float>(countCloseFlockmates);
        }
    }

    separatingForce = vector2::normalized(separatingForce);

    return separatingForce;
}

bool SeparationRule::drawImguiRuleExtra()
{
    bool valusHasChanged = false;

    if (ImGui::DragFloat("Desired Separation", &desiredMinimalDistance, 0.05f))
    {
        valusHasChanged = true;
    }

    return valusHasChanged;
}

sf::Vector2f AlignmentRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{
    //Try to match the heading of neighbors = Average velocity

    sf::Vector2f averageVelocity;

    if (neighbordhood.size() > 0)
    {

        for (auto const& flockmate : neighbordhood)
        {
            averageVelocity += (flockmate)->getVelocity();
        }

        averageVelocity /= static_cast<float>(neighbordhood.size());
    }

    return utils::vector2::normalized(averageVelocity);
}

///WIND 

sf::Vector2f WindRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{
    return utils::vector2::normalized(utils::vector2::getVector2FromRadian(windAngle));
}

bool WindRule::drawImguiRuleExtra()
{
    bool valusHasChanged = false;

    if (ImGui::SliderAngle("Wind Direction", &windAngle, 0))
    {
        valusHasChanged = true;
    }

    return valusHasChanged;
}

/// MOUSE

sf::Vector2f MouseInfluenceRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{

    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMousePosValid() && io.MouseDown[0])
    {
        sf::Vector2f mousePos(io.MousePos.x, io.MousePos.y);
        sf::Vector2f displacement = mousePos - boid->getPosition();
        float distance = utils::vector2::getMagnitude(displacement);

        //The force is inversely proportional to distance

        sf::Vector2f force = displacement / exp(distance / 100.f);

        if (isRepulsive)
        {
            force *= -1.f;
        }

        return force;
    }
    else
    {
        return sf::Vector2f();
    }

}

bool MouseInfluenceRule::drawImguiRuleExtra()
{

    bool valusHasChanged = false;

    if (ImGui::RadioButton("Attractive", !isRepulsive))
    {
        isRepulsive = false;
        valusHasChanged = true;
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Repulsive", isRepulsive))
    {
        isRepulsive = true;
        valusHasChanged = true;
    }

    return valusHasChanged;
}

sf::Vector2f BoundedAreaRule::computeForce(const std::vector<Boid*>& neighbordhood, Boid* boid)
{
    //Return a force proportional to the proximity of the boids with the bounds, and opposed to it

    sf::Vector2f force; //zero
    sf::Vector2f position = boid->getPosition();

    float epsilon = 0.00001f;  //avoid div by zero

    //Too close from min
    if (position.x < desiredDistance)
    {
        force.x += desiredDistance / position.x; //car position.x = distance de 0 à x.
    }
        //Too close from max
    else if (position.x > widthWindows - desiredDistance)
    {
        int d = position.x - widthWindows;
        //if (d == 0) d = 0.0001; //avoid div by zero
        force.x += desiredDistance / (d + epsilon);
    }

    //Too close from min
    if (position.y < desiredDistance)
    {
        force.y += desiredDistance / position.y;
    }
        //Too close from max
    else if (position.y > heightWindows - desiredDistance)
    {
        int d = position.y - heightWindows;
        //if (d == 0) d = 0.0001; //avoid div by zero
        force.y += desiredDistance / (d + epsilon);
    }

    return force;
}

bool BoundedAreaRule::drawImguiRuleExtra()
{

    bool valusHasChanged = false;

    //We cap the max separation as the third of the min of the width.height
    int minHeightWidth = std::min(widthWindows, heightWindows);
    if (ImGui::SliderInt("Desired Distance From Borders", &desiredDistance, 0.0f, minHeightWidth / 3, "%i"))
    {
        valusHasChanged = true;
    }

    return valusHasChanged;
}

void BoundedAreaRule::draw(const Boid& boid, sf::RenderTarget& target, sf::RenderStates states) const
{

    FlockingRule::draw(boid, target, states);

    //Draw a rectangle on the map

    sf::RectangleShape bound(sf::Vector2f(widthWindows - 2 * desiredDistance, heightWindows - 2 * desiredDistance));
    bound.setOutlineColor(debugColor);
    bound.setOutlineThickness(1); //1 pixel outline
    bound.setFillColor(sf::Color::Transparent); //Transparent inside, we just draw the box bound
    bound.setPosition(desiredDistance, desiredDistance);

    target.draw(bound, states);
}