#ifndef UTILS_H
#define UTILS_H

#define _USE_MATH_DEFINES //Pour utiliser PI

//UniqueVector
#include <vector>
#include <memory>
#include <deque>
#include "Vector3.h"
#include "SDL.h"
#include "Vector2.h"

namespace utils {

    namespace graphics {

        // kExtra colors for SFML
        // make all colors be of the stuct collor 
        static const Vector3 Gray(130, 130, 130);
        static const Vector3 LightRed(245, 88, 122);
        static const Vector3 Orange(255, 107, 15);
        static const Vector3 LightOrange(245, 197, 66);
        static const Vector3 LimeGreen(140, 245, 66);
        static const Vector3 LightBlue(66, 188, 245);
        static const Vector3 DarkBlue(91, 15, 255);
        static const Vector3 Purple(138, 66, 245);
        static const Vector3 Pink(245, 88, 232);

        /* Draw a line SFML, show the given vector from the starting point with the given color */
        void drawVector(SDL_Renderer* renderer, Vector2 start, Vector2 vector, Vector3 color);
    }
}
#endif

#pragma clang diagnostic pop