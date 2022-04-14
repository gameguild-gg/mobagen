#ifndef PACTICLE_H
#define PACTICLE_H

#include "Utils.h"
#include "GameObject.h"
#include "Transform.h"

class Particle : public GameObject {
private:
    //Members
    bool hasConstantSpeed = false;
    float speed = 120.;
    float maxAcceleration = 10.;
    float circleSize; // todo: is this needed?

    Vector3 color;
//    Vector2 velocity;
    Vector2 acceleration;
    Vector2 previousAcceleration; //to draw Acc

    //Methods
    void resetAcceleration();

public:
    Vector2 velocity;
    bool drawAcceleration = false;

    //Constructor
    explicit Particle(Engine *pEngine, float size = 4.f, Vector3 color = Vector3::Green());

    //Getter / Setters
    Transform getTransform() const {
        return transform;
    }

    Vector2 getPosition() const {
        return transform.position;
    }

    Vector2 getVelocity() const {
        return velocity;
    }

    void setPosition(Vector2 position) {
        transform.position = position;
    }

    void setVelocity(Vector2 velocity_) {
        transform.rotation = velocity_.normalized();
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

    void applyForce(Vector2 force);

//    virtual void update(const float deltaTime);

    virtual void UpdatePosition(const float deltaTime);


    // Inherited via Drawable
    virtual void OnDraw(SDL_Renderer* renderer) override;

};

#endif
