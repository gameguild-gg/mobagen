//#include "SFMLApp.h"
//
//#include <iostream>
//#include "Boid.h"
//#include "ImGuiExtra.h"
//
////Windows include for performance calculation
//#if defined(_WIN32)
//#include "windows.h"
//#include "psapi.h"
//#endif
//
//#include <imgui.h>
//
//using namespace utils;
//
//
//int SFMLApp::run()
//{
//    /// Initialization windows & settings
//    sf::ContextSettings settings;
//    settings.antialiasingLevel = antialiasing;
//
//    sf::RenderWindow window(sf::VideoMode(widthWindow, heightWindow), "Boids Simulation", sf::Style::Default, settings);
//    window.setFramerateLimit(maxFramerate);
//    ImGui::SFML::Init(window);
//
//    //INITIALIZE WORLD
//    world = std::make_unique<World>(&window);
//
//    sf::Clock deltaClock;
//
//    /// MAIN LOOP
//    while (window.isOpen())
//    {
//        //Time between each frames
//        deltaTime = deltaClock.restart();
//
//        ///EVENTS
//        sf::Event event;
//        while (window.pollEvent(event))
//        {
//            ImGui::SFML::ProcessEvent(event);
//
//            if (event.type == sf::Event::Closed)
//                window.close();
//        }
//
//        ///REALTIME INPUTS
//        sf::Vector2f axisInput = getDirectionFromKeyboardInputs();
//        if (axisInput != sf::Vector2f() && world->getNbBoids() > 0)
//        {
//            Boid* firstBoid = *world->getAllBoids()->begin();
//            firstBoid->applyForce(axisInput * 20.f);
//        }
//
//        ///UPDATE LOGIC
//
//        //update each boid logic
//        world->update(deltaTime.asSeconds());
//
//        /* Update logic and position are separated so the movement calculations don't
//        take into account the new positions of other boids */
//
//        //Update their position
//        world->updatePositions(deltaTime.asSeconds());
//
//        ///IMGUI UI
//
//        ImGui::SFML::Update(window, deltaTime);
//
//        //ImGui::ShowDemoWindow();
//        showConfigurationWindow();
//
//
//        ///DRAW SCENE
//
//        window.clear();
//
//        //Draw each boid
//        for (auto& b : *world->getAllBoids())
//        {
//            b->draw(window);
//        }
//
//        ImGui::SFML::Render(window);
//
//        window.display();
//    }
//
//    ImGui::SFML::Shutdown();
//    return 0;
//}