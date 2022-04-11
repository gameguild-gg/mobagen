#include "Utils.h"
#include "Vector2.h"

namespace utils {
    namespace graphics {
        void drawVector(SDL_Renderer* renderer, Vector2 start, Vector2 vector, Vector3 color) {
            SDL_SetRenderDrawColor(renderer,
                                   (uint8_t)color.x,
                                   (uint8_t)color.y,
                                   (uint8_t)color.z,
                                   SDL_ALPHA_OPAQUE);
            Vector2 end = start + vector;
            SDL_RenderDrawLine(renderer,
                                   (int)start.x,
                                   (int)start.y,
                                   (int)end.x,
                                   (int)end.y);
        }
    }
}