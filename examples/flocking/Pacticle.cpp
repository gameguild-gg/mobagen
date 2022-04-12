#include "Pacticle.h"

Particle::Particle(Engine *pEngine, float size, Vector3 color): GameObject(pEngine) {
    transform = sf::CircleShape(size, 3);
    shape.setFillColor(color);
    shape.setOrigin(shape.getRadius(), shape.getRadius());
    shape.setPosition(sf::Vector2f());

    velocity = sf::Vector2f();
    acceleration = sf::Vector2f();
}


//Public Methods


void Particle::applyForce(Vector2 force)
{
    acceleration += force;
}

//void Particle::update(const float deltaTime)
//{
//
//    //Compute acceleration
//
//}

void Particle::updatePosition(const float deltaTime)
{
    //Has a max acceleration per frame
    if (Vector2::getMagnitude(acceleration) > maxAcceleration){
        acceleration = Vector2::normalized(acceleration) * maxAcceleration;
    }

    //Apply acceleration to velocity
    setVelocity(velocity + acceleration);

    resetAcceleration();

    //constant speed never change
    if (hasConstantSpeed) {
        setVelocity(Vector2::normalized(velocity) * speed);
    }
    //else only ceil the speed if wanted
    else if (Vector2::getMagnitude(velocity) > speed) {
        setVelocity(Vector2::normalized(velocity) * speed);
    }

    transform.position += velocity * deltaTime;
}

void Particle::OnDraw(SDL_Renderer* renderer)
{
    // todo: draw a circle
//    target.draw(shape);

    if (drawAcceleration)
    {
        sf::Color accelerationColor = utils::graphics::Purple;
        utils::graphics::drawVector(target, states, getPosition(), previousAcceleration * 1.5f, accelerationColor);
    }
}

//Private Methods

void Particle::resetAcceleration()
{
    previousAcceleration = acceleration;
    acceleration = Vector2::zero();
}