#include "Pacticle.h"

Particle::Particle(float size, sf::Color color)
{
    this->shape = sf::CircleShape(size, 3);
    shape.setFillColor(color);
    shape.setOrigin(shape.getRadius(), shape.getRadius());
    shape.setPosition(sf::Vector2f());

    velocity = sf::Vector2f();
    acceleration = sf::Vector2f();

}


//Public Methods


void Particle::applyForce(sf::Vector2f force)
{
    acceleration += force;
}

void Particle::update(const float deltaTime)
{

    //Compute acceleration

}

void Particle::updatePosition(const float deltaTime)
{

    //Has a max acceleration per frame
    if (utils::vector2::getMagnitude(acceleration) > maxAcceleration)
    {
        acceleration = utils::vector2::normalized(acceleration) * maxAcceleration;
    }

    //Apply acceleration to velocity
    setVelocity(velocity + acceleration);

    resetAcceleration();

    //constant speed never change
    if (hasConstantSpeed)
    {
        setVelocity(utils::vector2::normalized(velocity) * speed);
    }
        //else only ceil the speed if wanted
    else if (utils::vector2::getMagnitude(velocity) > speed)
    {
        setVelocity(utils::vector2::normalized(velocity) * speed);
    }

    shape.move(velocity * deltaTime);
}

void Particle::draw(sf::RenderTarget& target, sf::RenderStates states) const
{
    target.draw(shape);

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
    acceleration = sf::Vector2f();
}