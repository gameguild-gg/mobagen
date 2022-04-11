#include "World.h"

#include "imgui.h"

#include "Utils.h"
#include "ImGuiExtra.h"

using namespace utils;

void World::initializeRules()
{
    //Starting Rules
    boidsRules.emplace_back(std::make_unique<SeparationRule>(25., 4.75));
    boidsRules.emplace_back(std::make_unique<CohesionRule>(4.25));
    boidsRules.emplace_back(std::make_unique<AlignmentRule>(2.9));
    boidsRules.emplace_back(std::make_unique<MouseInfluenceRule>(2.));
    boidsRules.emplace_back(std::make_unique<BoundedAreaRule>(
            windowPtr->getSize().y, windowPtr->getSize().x, 80, 3.5, false));
    boidsRules.emplace_back(std::make_unique<WindRule>(1., 6.f, false));

    //Starting weights are saved as defaults
    defaultWeights = new float[boidsRules.size()];
    int i = 0;
    for (const auto& rule : boidsRules)
    {
        defaultWeights[i++] = rule->weight;
    }

}

void World::applyFlockingRulesToAllBoids()
{
    for (const auto& boid : boids)
    {
        boid->setFlockingRules(boidsRules);
    }
}

void World::setNumberOfBoids(int number)
{
    int diff = boids.size() - number;

    if (diff == 0)
    {
        return;
    }
        //Need to add boids
    else if (diff < 0)
    {
        //Back to positive
        diff = -diff;

        //Add boids equal to diff
        for (int i = 0; i < diff; i++)
        {
            BoidPtr boidPtr = createBoid();
            cachedBoids.push_back(boidPtr.get()); //lookup list
            boids.push_back(std::move(boidPtr)); //owning list
        }
    }
        //Too much boid, remove them
    else
    {
        //Remove from end
        for (int i = 0; i < diff; i++)
        {
            boids.pop_back();
            cachedBoids.pop_back();
        }

    }
}

void World::randomizeBoidPositionAndVelocity(Boid* boid)
{
    boid->setPosition(vector2::getRandom(windowPtr->getSize().x, windowPtr->getSize().y));
    boid->setVelocity(vector2::getVector2FromDegree(rand() % 360) * desiredSpeed); //Random dir
}

void World::warpParticleIfOutOfBounds(Particle* particle)
{
    //Correct position with windows borders
    sf::Vector2f position = particle->getShape().getPosition();
    sf::Vector2u sizeWindow = windowPtr->getSize();

    if (position.x < 0) {
        position.x += sizeWindow.x;
    }
    else if (position.x > sizeWindow.x) {
        position.x -= sizeWindow.x;
    }

    if (position.y < 0) {
        position.y += sizeWindow.y;
    }
    else if (position.y > sizeWindow.y) {
        position.y -= sizeWindow.y;
    }

    //If the position changed
    if (position != particle->getShape().getPosition())
    {
        particle->setPosition(position);
    }
}

BoidPtr World::createBoid()
{
    //Create new boid
    BoidPtr boid = std::make_unique<Boid>(&cachedBoids); //TODO : CHANGE

    randomizeBoidPositionAndVelocity(boid.get());
    boid->setFlockingRules(boidsRules);
    boid->setDetectionRadius(detectionRadius);
    boid->setSpeed(desiredSpeed);
    boid->setHasConstantSpeed(hasConstantSpeed);
    boid->drawAcceleration = showAcceleration;
    boid->drawDebugRadius = showRadius;
    boid->drawDebugRules = showRules;

    return boid;
}

World::World(sf::RenderWindow* windowPtr_) : windowPtr(windowPtr_)
{
    initializeRules();

    setNumberOfBoids(nbBoids);

    applyFlockingRulesToAllBoids();
}

std::vector<Boid*>* World::getAllBoids()
{
    return &cachedBoids;
}

void World::drawGeneralUI()
{
    ImGui::SetNextItemOpen(true, ImGuiCond_Once); //Next header is opened by default
    if (ImGui::CollapsingHeader("General"))
    {

        if (ImGui::DragInt("Number of Boids", &nbBoids))
        {
            if (nbBoids < 0)
                nbBoids = 0;
            setNumberOfBoids(nbBoids);
        }

        ImGui::SameLine(); HelpMarker("Drag to change the weight's value or CTRL+Click to input a new value.");

        if (ImGui::SliderFloat("Neighborhood Radius", &detectionRadius, 0.0f, 250.0f, "%.f"))
        {
            for (const auto& boid : boids)
            {
                boid->setDetectionRadius(detectionRadius);
            }
        }

        //Speeds

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::TreeNode("Movement Settings"))
        {

            if (ImGui::Checkbox("Has Constant Speed", &hasConstantSpeed))
            {
                for (const auto& boid : boids)
                {
                    boid->setHasConstantSpeed(hasConstantSpeed);
                }
            }

            const char* speedLabel = hasConstantSpeed ? "Speed" : "Max Speed";
            if (ImGui::SliderFloat(speedLabel, &desiredSpeed, 0.0f, 300.0f, "%.f"))
            {
                for (const auto& boid : boids)
                {
                    boid->setSpeed(desiredSpeed);
                }
            }

            //Acceleration

            if (ImGui::Checkbox("Has Max Acceleration", &hasMaxAcceleration))
            {
                for (const auto& boid : boids)
                {
                    if (hasMaxAcceleration)
                    {
                        boid->setMaxAcceleration(maxAcceleration);
                    }
                    else {
                        boid->setMaxAcceleration(10000.);
                    }
                }
            }
            ImguiTooltip("Boids keeps more momentum when the acceleration is capped.");

            if (hasMaxAcceleration)
            {
                if (ImGui::SliderFloat("Max Acceleration", &maxAcceleration, 0.0f, 35.0f, "%.f"))
                {
                    for (const auto& boid : boids)
                    {
                        boid->setMaxAcceleration(maxAcceleration);
                    }
                }
            }

            ImGui::TreePop();
        }


        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode("Display Settings"))
        {

            if (ImGui::Checkbox("Show Acceleration", &showAcceleration))
            {
                for (const auto& boid : boids)
                {
                    boid->drawAcceleration = showAcceleration;
                }
            }

            if (ImGui::Checkbox("Show Radius", &showRadius))
            {
                for (const auto& boid : boids)
                {
                    boid->drawDebugRadius = showRadius;
                }
            }

            if (ImGui::Checkbox("Show Rules", &showRules))
            {
                for (const auto& boid : boids)
                {
                    boid->drawDebugRules = showRules;
                }
            }

            ImGui::TreePop();
        }

        if (ImGui::Button("Randomize Boids position and velocity"))
        {
            for (const auto& boid : boids)
            {
                randomizeBoidPositionAndVelocity(boid.get());
            }
        }
    }
}

void World::drawRulesUI()
{
    if (ImGui::CollapsingHeader("Rules"))
    {
        for (auto& rule : boidsRules)
        {

            if (rule->drawImguiRule()) //display the UI and returns true if a value has been changed
            {
                applyFlockingRulesToAllBoids();
            }
            ImGui::Separator();

        }

        if (ImGui::Button("Restore Default Weights"))
        {
            int i = 0;
            //restore default values
            for (auto& rule : boidsRules)
            {
                rule->weight = defaultWeights[i++];
            }
            applyFlockingRulesToAllBoids();
        }

        ImGui::Spacing();
    }
}

void World::update(float deltaTime)
{
    for (auto& b : boids) {
        b->update(deltaTime);
    }
}

void World::updatePositions(float deltaTime)
{
    for (auto& b : boids) {

        b->updatePosition(deltaTime);
        warpParticleIfOutOfBounds(b.get());
    }
}