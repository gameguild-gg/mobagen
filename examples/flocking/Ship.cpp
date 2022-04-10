#include "Ship.h"
#include "Random.h"
#include<cstdlib>

Ship::Ship(Engine *pEngine) : GameObject(pEngine) {
    polygon.transform.rotation = {0,1};
    polygon.transform.scale = {10,10};
    polygon.transform.position = {300,300};
    polygon.points = {{0,2}, {1,-1}, {0,0}, {-1,-1}};

    Random rand;
    color = Vector3(
            rand.Range(127,255),
            rand.Range(127,255),
            rand.Range(127,255));
}

void Ship::OnDraw(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer,
                           (uint8_t)color.x,
                           (uint8_t)color.y,
                           (uint8_t)color.z,
                           SDL_ALPHA_OPAQUE);
    auto drawablePoints = polygon.getDrawablePoints();
    for(int i=0; i<drawablePoints.size(); i++) {
        int other = i+1;
        if(other == drawablePoints.size())
            other = 0;
        SDL_RenderDrawLine(renderer,
                           (int)drawablePoints[i].x,
                           (int)drawablePoints[i].y,
                           (int)drawablePoints[other].x,
                           (int)drawablePoints[other].y);
    }
}

void Ship::Update(float deltaTime) {
    polygon.transform.rotation = polygon.transform.rotation.Rotate(deltaTime*10);
}
