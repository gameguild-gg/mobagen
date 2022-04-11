#ifndef PACTICLE_H
#define PACTICLE_H

#include "Utils.h"

class Particle : public sf::Drawable
{

private:

    //Members

    bool hasConstantSpeed = false;
    float speed = 120.;

    float maxAcceleration = 10.;


    sf::Vector2f velocity;
    sf::Vector2f acceleration;
    sf::Vector2f previousAcceleration; //to draw Acc

    //Methods
    void resetAcceleration();

protected:

    sf::CircleShape shape;

public:


    bool drawAcceleration = false;

    //Constructor

    Particle(float size = 4.f, sf::Color color = sf::Color::Green);

    //Getter / Setters
    sf::CircleShape getShape() const {
        return shape;
    }

    sf::Vector2f getPosition() const {
        return shape.getPosition();
    }

    sf::Vector2f getVelocity() const {
        return velocity;
    }

    void setPosition(sf::Vector2f position) {
        shape.setPosition(position);
    }

    void setVelocity(sf::Vector2f velocity_) {
        velocity = velocity_;

        shape.setRotation(utils::vector2::getAngleDegree(velocity));
    }

    void setSpeed(float newSpeed) {
        speed = newSpeed;
    }

    void setMaxAcceleration(float newMaxAcceleration)
    {
        maxAcceleration = newMaxAcceleration;
    }

    void setHasConstantSpeed(bool hasConstantSpeed_)
    {
        hasConstantSpeed = hasConstantSpeed_;
    }


    //Methods

    void applyForce(sf::Vector2f force);

    virtual void update(const float deltaTime);

    virtual void updatePosition(const float deltaTime);


    // Inherited via Drawable
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states = sf::RenderStates::Default) const override;

};

#endif //MOBAGEN_PACTICLE_H
