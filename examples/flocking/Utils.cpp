#include "Utils.h"

namespace utils {
    namespace graphics {
        void drawVector(SDL_Renderer* renderer, const Vector2& start, const Vector2& vector, const Vector3& color) {
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