#include "Ship.h"

Ship::Ship(Engine *pEngine) : GameObject(pEngine) {
    polygon.rotation = 0;
    polygon.scale = 100;
    polygon.position = {500,500};
    polygon.points = {{0,2}, {1,-1},{0,0},{-1,-1}};
//    polygon.points = {{1000,1000}, {1,-1},{0,0},{-1,-1}};
}

void Ship::OnDraw(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    auto drawablePoints = polygon.getDrawablePoints();
    for(int i=0; i<drawablePoints.size(); i++) {
        int other = i+1;
        if(other == drawablePoints.size())
            other = 0;
        SDL_RenderDrawLine(renderer,
                           (int)drawablePoints[i].x,
                           (int)drawablePoints[i].y,
                           (int)drawablePoints[other].x,
                           (int)drawablePoints[other].x);
    }
}
