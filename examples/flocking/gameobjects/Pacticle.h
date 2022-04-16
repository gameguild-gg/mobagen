#ifndef PACTICLE_H
#define PACTICLE_H

#include "GameObject.h"
#include "Transform.h"
#include "Polygon.h"

class Particle : public GameObject {
private:
    //Members
    bool hasConstantSpeed = false;
    float speed = 120.;
    float maxAcceleration = 10.;
    float circleSize; // todo: is this really needed?

    Vector2 acceleration;
    Vector2 previousAcceleration; //to draw Acc
    Polygon polygon;

    //Methods
    void resetAcceleration();

public:
    Vector2 velocity; // todo: make this private
    bool drawAcceleration = false;
    Vector3 color; // todo: make this private

    //Constructor
    explicit Particle(Engine *pEngine, float size = 4.f, Vector3 color = Vector3::RandomColor(31,255));

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
        velocity = velocity_;
        transform.rotation = velocity_.normalized();
    }

    void setSpeed(float newSpeed) {
        speed = newSpeed;
    }

    void setMaxAcceleration(float newMaxAcceleration) {
        maxAcceleration = newMaxAcceleration;
    }

    void setHasConstantSpeed(bool hasConstantSpeed_) {
        hasConstantSpeed = hasConstantSpeed_;
    }

    //Methods
    void applyForce(Vector2 force);

    virtual void Update(float deltaTime) override;

    // Inherited via Drawable
    void OnDraw(SDL_Renderer* renderer) override;
};

#endif
