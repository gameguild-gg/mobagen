#include "SFMLApp.h"

#include <iostream>
#include "Boid.h"
#include "ImGuiExtra.h"

//Windows include for performance calculation
#if defined(_WIN32)
#include "windows.h"
#include "psapi.h"
#endif

#include <imgui.h>

using namespace utils;

sf::Vector2f SFMLApp::getDirectionFromKeyboardInputs()
{

    sf::Vector2f direction;

    //Real-time input
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
    {
        direction += vector2::left();
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
    {
        direction += vector2::right();
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up))
    {
        direction += vector2::up();
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down))
    {
        direction += vector2::down();
    }

    direction = vector2::normalized(direction);

    return direction;
}


void SFMLApp::showConfigurationWindow()
{
    //Resized once at first windows appearance
    ImGui::SetNextWindowPos(ImVec2(850, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 550), ImGuiCond_Once);

    ImGui::Begin("Configuration"); // begin window

    ImGui::Text("Control the simulation with those settings.");
    ImGui::Spacing();
    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.45f);

    world->drawGeneralUI();

    //ImGui::SetNextItemOpen(true, ImGuiCond_Once); //Next header is opened by default

    world->drawRulesUI();

    drawPerformanceUI();

    ImGui::End(); // end window
}

void SFMLApp::drawPerformanceUI()
{
    if (ImGui::CollapsingHeader("Performance"))
    {
        //The  functions called are Windows specific
#if defined(_WIN32)

        ///FPS Count
        float framePerSecond = 1. / deltaTime.asSeconds();
        ImGui::Text("Frames Per Second (FPS) : %.f", framePerSecond);
        PlotVar("Frame duration (ms)", deltaTime.asMilliseconds());
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

        //ImGui::Text("Virtual Memory Currently Used : %iMb \n", virtualMemUsed / div);
        //ImGui::Text("RAM Currently Used : %uMb \n", physMemUsed / div);
        //ImGui::Separator();

        ImGui::Text("Virtual Memory used by process : %uMb \n", virtualMemUsedByMe / div);
        PlotVar("Virtual Memory Consumption (Mb)", virtualMemUsedByMe / div);

        ImGui::Text("RAM used by process : %uMb \n", physMemUsedByMe / div);
        PlotVar("Ram Consumption (Mb)", physMemUsedByMe / div);

#else
        ImGui::Text("Only implemented on Windows.");
#endif
    }
}


int SFMLApp::run()
{
    /// Initialization windows & settings
    sf::ContextSettings settings;
    settings.antialiasingLevel = antialiasing;

    sf::RenderWindow window(sf::VideoMode(widthWindow, heightWindow), "Boids Simulation", sf::Style::Default, settings);
    window.setFramerateLimit(maxFramerate);
    ImGui::SFML::Init(window);

    //INITIALIZE WORLD
    world = std::make_unique<World>(&window);

    sf::Clock deltaClock;

    /// MAIN LOOP
    while (window.isOpen())
    {
        //Time between each frames
        deltaTime = deltaClock.restart();

        ///EVENTS
        sf::Event event;
        while (window.pollEvent(event))
        {
            ImGui::SFML::ProcessEvent(event);

            if (event.type == sf::Event::Closed)
                window.close();
        }

        ///REALTIME INPUTS
        sf::Vector2f axisInput = getDirectionFromKeyboardInputs();

        if (axisInput != sf::Vector2f() && world->getNbBoids() > 0)
        {
            Boid* firstBoid = *world->getAllBoids()->begin();
            firstBoid->applyForce(axisInput * 20.f);
        }

        ///UPDATE LOGIC

        //update each boid logic
        world->update(deltaTime.asSeconds());

        /* Update logic and position are separated so the movement calculations don't
        take into account the new positions of other boids */

        //Update their position
        world->updatePositions(deltaTime.asSeconds());

        ///IMGUI UI

        ImGui::SFML::Update(window, deltaTime);

        //ImGui::ShowDemoWindow();
        showConfigurationWindow();


        ///DRAW SCENE

        window.clear();

        //Draw each boid
        for (auto& b : *world->getAllBoids())
        {
            b->draw(window);
        }

        ImGui::SFML::Render(window);

        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}