#include "Pacticle.h"
#include <Polygon.h>

Particle::Particle(Engine *pEngine, float size, Vector3 color): GameObject(pEngine) {
    circleSize = size;
    this->color = color;
    velocity = Vector2::zero();
    acceleration = Vector2::zero();
    polygon.points = {{0,2}, {1,-1}, {0,0}, {-1,-1}};
    transform.scale = {3,3};
}

//Public Methods
void Particle::applyForce(Vector2 force) {
    acceleration += force;
}

void Particle::UpdatePosition(const float deltaTime) {
    //Has a max acceleration per frame
    if (Vector2::getMagnitude(acceleration) > maxAcceleration)
        acceleration = Vector2::normalized(acceleration) * maxAcceleration;

    //Apply acceleration to velocity
    setVelocity(velocity + acceleration);
    resetAcceleration();

    // constant speed never change || else only ceil the speed if wanted
    if (hasConstantSpeed || Vector2::getMagnitude(velocity) > speed)
        setVelocity(Vector2::normalized(velocity) * speed);

    transform.position += velocity * deltaTime;
}

void Particle::OnDraw(SDL_Renderer* renderer) {
    // todo: draw a circle or the ship?
    polygon.Draw(renderer, transform, color);

    if (drawAcceleration) {
        Vector3 accelerationColor = Vector3::Purple();
        utils::graphics::drawVector(renderer, getPosition(), previousAcceleration * 1.5f, accelerationColor);
    }
}

void Particle::resetAcceleration() {
    previousAcceleration = acceleration;
    acceleration = Vector2::zero();
}