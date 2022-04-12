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
        /* Draw a line SFML, show the given vector from the starting point with the given color */
        void drawVector(SDL_Renderer* renderer, Vector2 start, Vector2 vector, Vector3 color);
    }
}
#endif

#pragma clang diagnostic pop