#include "Polygon.h"
#include "ColorT.h"

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

void Polygon::Draw(SDL_Renderer *renderer, const Transform& transform, const Color32& color) {
    SDL_SetRenderDrawColor(renderer,
                           color.r,
                           color.g,
                           color.b,
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

void Polygon::DrawLine(SDL_Renderer *renderer, const Vector2 &v1, const Vector2 &v2, const Color32& color) {
    SDL_SetRenderDrawColor(renderer,
                           color.r,
                           color.g,
                           color.b,
                           SDL_ALPHA_OPAQUE);
    SDL_RenderDrawLine(renderer,(int) v1.x,(int) v1.y,(int) v2.x,(int) v2.y);
}

void Polygon::Draw(SDL_Renderer *renderer, const Vector2 &position, const Vector2 &scale, const Vector2 &rotation,
                   const Color32 &color) {
    Transform transform = {position,scale,rotation};
    Draw(renderer, transform, color);
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

Hexagon::Hexagon() {
    points.push_back(Vector2::up().Rotate(0));
    points.push_back(Vector2::up().Rotate(60));
    points.push_back(Vector2::up().Rotate(120));
    points.push_back(Vector2::up().Rotate(180));
    points.push_back(Vector2::up().Rotate(240));
    points.push_back(Vector2::up().Rotate(300));
}
