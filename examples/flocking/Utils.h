#include "SDL.h"
#include "Vector2.h"
#include "Vector3.h"

#ifndef UTILS_H
#define UTILS_H

#define _USE_MATH_DEFINES

//UniqueVector

namespace utils {

    namespace graphics {
        /* Draw a line SFML, show the given vector from the starting point with the given color */
        void drawVector(SDL_Renderer* renderer, Vector2 start, Vector2 vector, Vector3 color);
    }
}
#endif

#pragma clang diagnostic pop