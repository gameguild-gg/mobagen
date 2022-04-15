#include "Ship.h"
#include "Random.h"

Ship::Ship(Engine *pEngine) : GameObject(pEngine) {
    transform.rotation = {0,1};
    transform.scale = {10,10};
    transform.position = {300,300};
    polygon.points = {{0,2}, {1,-1}, {0,0}, {-1,-1}};
    
    color = Vector3(
            Random::Range(31,255),
            Random::Range(31,255),
            Random::Range(31,255));
}

void Ship::OnDraw(SDL_Renderer* renderer) {
    polygon.Draw(renderer, transform, color);
}

void Ship::Update(float deltaTime) {
    transform.rotation = transform.rotation.Rotate(deltaTime*10);
}
