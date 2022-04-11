#include "Utils.h"

namespace utils {

    namespace graphics {


        void drawVector(sf::RenderTarget& target, sf::RenderStates states, const sf::Vector2f start, const sf::Vector2f vector, const sf::Color color)
        {

            sf::VertexArray line = sf::VertexArray(sf::Lines, 2);
            line[0] = start;
            line[1] = start + vector;

            line[0].color = color;
            line[1].color = color;

            target.draw(line, states);
        }
    }
}