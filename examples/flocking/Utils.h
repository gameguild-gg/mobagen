#ifndef UTILS_H
#define UTILS_H

#include "SDL.h"
#include "Vector2.h"
#include "Vector3.h"

#define _USE_MATH_DEFINES

//UniqueVector

namespace utils {
    namespace graphics {
        /* Draw a line SFML, show the given vector from the starting point with the given color */
        void drawVector(SDL_Renderer* renderer, const Vector2& start, const Vector2& vector, const Vector3& color);
    }
}
#endif