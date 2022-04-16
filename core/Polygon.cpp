#include "Polygon.h"

std::vector<Vector2> Polygon::getDrawablePoints(const Transform& transform) {
    std::vector<Vector2> ret;
    for(auto p : points) {
        auto scaled = Vector2(transform.scale.x*p.x, transform.scale.y*p.y);
        auto rotated = scaled.Rotate(transform.rotation.getAngleDegree());
        auto displaced = rotated + transform.position;
        ret.push_back(displaced);
    }
    return ret;
}

void Polygon::Draw(SDL_Renderer *renderer, const Transform& transform, const Vector3& color) {
    SDL_SetRenderDrawColor(renderer,
                           (uint8_t)color.x,
                           (uint8_t)color.y,
                           (uint8_t)color.z,
                           SDL_ALPHA_OPAQUE);
    auto drawablePoints = getDrawablePoints(transform);
    for(int i=0; i<drawablePoints.size(); i++) {
        int other = i + 1;
        if (other == drawablePoints.size())
            other = 0;
        SDL_RenderDrawLine(renderer,
                           (int) drawablePoints[i].x,
                           (int) drawablePoints[i].y,
                           (int) drawablePoints[other].x,
                           (int) drawablePoints[other].y);
    }
}

void Polygon::DrawLine(SDL_Renderer *renderer, const Vector2 &v1, const Vector2 &v2, const Vector3& color) {
    SDL_SetRenderDrawColor(renderer,
                           (uint8_t)color.x,
                           (uint8_t)color.y,
                           (uint8_t)color.z,
                           SDL_ALPHA_OPAQUE);
    SDL_RenderDrawLine(renderer,(int) v1.x,(int) v1.y,(int) v2.x,(int) v2.y);
}

void Polygon::Draw(SDL_Renderer *renderer, const Vector2 &position, const Vector2 &scale, const Vector2 &rotation,
                   const Vector3 &color) {
    Transform transform = {position,scale,rotation};
    SDL_SetRenderDrawColor(renderer,
                           (uint8_t)color.x,
                           (uint8_t)color.y,
                           (uint8_t)color.z,
                           SDL_ALPHA_OPAQUE);
    auto drawablePoints = getDrawablePoints(transform);
    for(int i=0; i<drawablePoints.size(); i++) {
        int other = i + 1;
        if (other == drawablePoints.size())
            other = 0;
        SDL_RenderDrawLine(renderer,
                           (int) drawablePoints[i].x,
                           (int) drawablePoints[i].y,
                           (int) drawablePoints[other].x,
                           (int) drawablePoints[other].y);
    }
}

Circle::Circle(int sample) {
    for (int i = 0; i < sample; i++) {
        Vector2 point = Vector2::up().Rotate(360.f * (float) i / (float) sample);
        points.push_back(point);
    }
}

Square::Square() {
    points.push_back(Vector2::up().Rotate(45));
    points.push_back(Vector2::up().Rotate(135));
    points.push_back(Vector2::up().Rotate(225));
    points.push_back(Vector2::up().Rotate(315));
}