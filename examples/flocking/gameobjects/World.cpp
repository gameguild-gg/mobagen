#include "World.h"
#include "imgui.h"
#include "../utils/ImGuiExtra.h"
#include "Random.h"

#include "../behaviours/SeparationRule.h"
#include "../behaviours/CohesionRule.h"
#include "../behaviours/AlignmentRule.h"
#include "../behaviours/MouseInfluenceRule.h"
#include "../behaviours/BoundedAreaRule.h"
#include "../behaviours/WindRule.h"

#if defined(_WIN32)
#include "Windows.h"
#include "Psapi.h"
#endif

World::World(Engine *pEngine): GameObject(pEngine) {}

void World::initializeRules() {
    auto size = engine->window->size();

    //Starting Rules
    boidsRules.emplace_back(std::make_unique<SeparationRule>(this, 25.f, 4.75f));
    boidsRules.emplace_back(std::make_unique<CohesionRule>(this, 4.25f));
    boidsRules.emplace_back(std::make_unique<AlignmentRule>(this, 2.9f));
    boidsRules.emplace_back(std::make_unique<MouseInfluenceRule>(this, 2.f));
    boidsRules.emplace_back(std::make_unique<BoundedAreaRule>(this, 20, 8.f, false));
    boidsRules.emplace_back(std::make_unique<WindRule>(this, 1.f, 6.f, false));

    //Starting weights are saved as defaults
    defaultWeights.clear();
    for (const auto& rule : boidsRules)
        defaultWeights.push_back(rule->weight);

    ImGui::SetCurrentContext(engine->imGuiContext);
    SetupImGuiStyle();
}

void World::applyFlockingRulesToAllBoids() {
    for (const auto& boid : boids)
        boid->setFlockingRules(boidsRules);
}

void World::setNumberOfBoids(int number) {
    int diff = (int)boids.size() - number;

    if (diff == 0)
        return;

    //Need to add boids
    else if (diff < 0) {
        //Back to positive
        diff = -diff;

        //Add boids equal to diff
        for (int i = 0; i < diff; i++)
            boids.push_back(createBoid());
    }

    //Too much boid, remove them
    else
        //Remove from end
        for (int i = 0; i < diff; i++) {
            auto go = boids.back();
            engine->Destroy(go);
            boids.pop_back();
        }
}

void World::randomizeBoidPositionAndVelocity(Boid* boid) {
    auto size = engine->window->size();
    // todo: give boid a transform to store position, rotation... etc
    boid->setPosition({Random::Range(0,size.x), Random::Range(0,size.y)});
    boid->setVelocity(Vector2::up().Rotate(Random::Range(0,360)) * desiredSpeed); //Random dir
}

void World::warpParticleIfOutOfBounds(Particle* particle) {
    //Correct position with windows borders
    Vector2 position = particle->getTransform().position;
    Vector2 sizeWindow = engine->window->size(); // todo: make it int

    if (position.x < 0)
        position.x += sizeWindow.x;
    else if (position.x > sizeWindow.x)
        position.x -= sizeWindow.x;

    if (position.y < 0)
        position.y += sizeWindow.y;
    else if (position.y > sizeWindow.y)
        position.y -= sizeWindow.y;

    //If the position changed
    if (position != particle->transform.position)
        particle->transform.position = position;
}

Boid* World::createBoid() {
    //Create new boid
    auto boid = new Boid(engine, this);

    randomizeBoidPositionAndVelocity(boid);
    boid->setFlockingRules(boidsRules);
    boid->setDetectionRadius(detectionRadius);
    boid->setSpeed(desiredSpeed);
    boid->setHasConstantSpeed(hasConstantSpeed);
    boid->drawAcceleration = showAcceleration;
    boid->drawDebugRadius = showRadius;
    boid->drawDebugRules = showRules;

    return boid;
}

std::vector<Boid*>* World::getAllBoids() {
    return &boids;
}

void World::drawGeneralUI() {
    ImGui::SetNextItemOpen(true, ImGuiCond_Once); //Next header is opened by default
    if (ImGui::CollapsingHeader("General")) {
        if (ImGui::DragInt("Number of Boids", &nbBoids)) {
            if (nbBoids < 0)
                nbBoids = 0;
            setNumberOfBoids(nbBoids);
        }

        ImGui::SameLine(); HelpMarker("Drag to change the weight's value or CTRL+Click to input a new value.");

        if (ImGui::SliderFloat("Neighborhood Radius", &detectionRadius, 0.0f, 250.0f, "%.f"))
            for (const auto& boid : boids)
                boid->setDetectionRadius(detectionRadius);

        //Speeds
        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::TreeNode("Movement Settings")) {
            if (ImGui::Checkbox("Has Constant Speed", &hasConstantSpeed))
                for (const auto& boid : boids)
                    boid->setHasConstantSpeed(hasConstantSpeed);


            const char* speedLabel = hasConstantSpeed ? "Speed" : "Max Speed";
            if (ImGui::SliderFloat(speedLabel, &desiredSpeed, 0.0f, 300.0f, "%.f"))
                for (const auto& boid : boids)
                    boid->setSpeed(desiredSpeed);


            //Acceleration
            if (ImGui::Checkbox("Has Max Acceleration", &hasMaxAcceleration)) {
                for (const auto& boid : boids) {
                    if (hasMaxAcceleration)
                        boid->setMaxAcceleration(maxAcceleration);
                    else
                        boid->setMaxAcceleration(10000.);
                }
            }
            ImguiTooltip("Boids keeps more momentum when the acceleration is capped.");

            if (hasMaxAcceleration)
                if (ImGui::SliderFloat("Max Acceleration", &maxAcceleration, 0.0f, 35.0f, "%.f"))
                    for (const auto& boid : boids)
                        boid->setMaxAcceleration(maxAcceleration);

            ImGui::TreePop();
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode("Display Settings")) {
            if (ImGui::Checkbox("Show Acceleration", &showAcceleration))
                for (const auto& boid : boids)
                    boid->drawAcceleration = showAcceleration;

            if (ImGui::Checkbox("Show Radius", &showRadius))
                for (const auto& boid : boids)
                    boid->drawDebugRadius = showRadius;

            if (ImGui::Checkbox("Show Rules", &showRules))
                for (const auto& boid : boids)
                    boid->drawDebugRules = showRules;

            ImGui::TreePop();
        }

        if (ImGui::Button("Randomize Boids position and velocity"))
            for (const auto& boid : boids)
                randomizeBoidPositionAndVelocity(boid);
    }
}

void World::drawRulesUI() {
    if (ImGui::CollapsingHeader("Rules")) {
        for (auto& rule : boidsRules) {
            if (rule->drawImguiRule()) //display the UI and returns true if a value has been changed
                applyFlockingRulesToAllBoids();
            ImGui::Separator();
        }

        if (ImGui::Button("Restore Default Weights")) {
            int i = 0;
            //restore default values
            for (auto& rule : boidsRules)
                rule->weight = defaultWeights[i++];

            applyFlockingRulesToAllBoids();
        }

        ImGui::Spacing();
    }
}

void World::Update(float deltaTime) {
    // move the first boid
    if (engine->getInputArrow() != Vector2::zero() && getNbBoids() > 0) {
        Boid* firstBoid = *getAllBoids()->begin();
        firstBoid->applyForce(engine->getInputArrow() * 20.f);
        firstBoid->drawDebugRadius = true;
        firstBoid->circleColor = Vector3::Red();
    }

    // update positions
    for (auto& b : boids)
        warpParticleIfOutOfBounds(b);
}

int World::getNbBoids() const {
    return nbBoids;
}

void World::showConfigurationWindow(float deltaTime) {
    if(ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")){
                engine->Exit();
            }
            ImGui::EndMenu();
        }

        ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
                    ImGui::GetIO().DeltaTime * 1000,
                    1.0f / ImGui::GetIO().DeltaTime,
                    1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);
        ImGui::EndMainMenuBar();
    }

    //Resized once at first windows appearance
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 550), ImGuiCond_Once);

    if(ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar )) { // begin window
        ImGui::Text("Control the simulation with those settings.");
        ImGui::Spacing();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.45f);

        drawGeneralUI();

        ImGui::SetNextItemOpen(true, ImGuiCond_Once); //Next header is opened by default

        drawRulesUI();

        drawPerformanceUI(deltaTime);

        ImGui::End(); // end window
    }
}

void World::drawPerformanceUI(float deltaTime) {
#if defined(_WIN32)
    if (ImGui::CollapsingHeader("Performance")) {
        //The  functions called are Windows specific
        ///FPS Count
        float framePerSecond = 1. / deltaTime;
        ImGui::Text("Frames Per Second (FPS) : %.f", framePerSecond);
        PlotVar("Frame duration (ms)", deltaTime*1000);
        ImGui::Separator();

        ///CPU and RAM
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);

        //Total Virtual Memory
        std::int64_t totalVirtualMem = memInfo.ullTotalPageFile;

        //Virtual Memory currently used
        std::int64_t virtualMemUsed = memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;

        //Virtual Memory current process
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
        std::size_t virtualMemUsedByMe = pmc.PrivateUsage;

        //Total RAM
        std::int64_t totalPhysMem = memInfo.ullTotalPhys;

        //Ram Currently Used
        std::int64_t physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

        //Ram Current used by Process
        std::size_t physMemUsedByMe = pmc.WorkingSetSize;

        int div = 1048576;

        //PC info
        ImGui::Text("Total Virtual Memory : %uMb \n", totalVirtualMem / div);
        ImGui::Text("Total RAM : %uMb \n", totalPhysMem / div);

        ImGui::Text("Virtual Memory Currently Used : %iMb \n", virtualMemUsed / div);
        ImGui::Text("RAM Currently Used : %uMb \n", physMemUsed / div);
        ImGui::Separator();

        ImGui::Text("Virtual Memory used by process : %uMb \n", virtualMemUsedByMe / div);
        PlotVar("Virtual Memory Consumption (Mb)", virtualMemUsedByMe / div);

        ImGui::Text("RAM used by process : %uMb \n", physMemUsedByMe / div);
        PlotVar("Ram Consumption (Mb)", physMemUsedByMe / div);

    }
#endif
}

void World::OnGui(ImGuiContext *context) {
    ImGui::SetCurrentContext(context);
    float deltaTime = ImGui::GetIO().DeltaTime;
    showConfigurationWindow(deltaTime);
}

void World::Start() {
    initializeRules();
    setNumberOfBoids(nbBoids);
    applyFlockingRulesToAllBoids();
}
