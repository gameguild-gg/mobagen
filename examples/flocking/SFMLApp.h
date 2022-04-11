#ifndef SFMLAPP_H
#define SFMLAPP_H


#include "World.h"

class Particle;


class SFMLApp
{


private:

    //Window Settings
    const int antialiasing = 8;
    const int maxFramerate = 60;

    const int widthWindow = 1200;
    const int heightWindow = 600;


    //Members

    //Configuration
    std::unique_ptr<World> world;

    //Data
    //Time between each frames
    sf::Time deltaTime;

    ///Methods
    sf::Vector2f getDirectionFromKeyboardInputs();

    //ImGui
    void showConfigurationWindow();
    void drawPerformanceUI();

public:

    SFMLApp() {};

    int run();

};


#endif
