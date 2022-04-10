#ifndef SHIP_H
#define SHIP_H

#include "GameObject.h"
#include "Polygon.h"
#include "Vector3.h"

class Ship: GameObject {
private:
    Polygon polygon;
    Vector3 color;
public:
    explicit Ship(Engine *pEngine);
    void OnDraw(SDL_Renderer* renderer) override;
    void Update(float deltaTime) override;
};

#endif
