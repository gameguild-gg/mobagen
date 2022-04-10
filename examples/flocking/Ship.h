#ifndef SHIP_H
#define SHIP_H

#include "GameObject.h"
#include "Polygon.h"

class Ship: GameObject {
private:
    Polygon polygon;
public:
    explicit Ship(Engine *pEngine);
    void OnDraw(SDL_Renderer* renderer) override;
    void Update(float deltaTime) override;
};

#endif
