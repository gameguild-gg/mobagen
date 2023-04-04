#ifndef PACTICLE_H
#define PACTICLE_H

#include "scene/GameObject.h"
#include "scene/Transform.h"
#include "Polygon.h"

class Particle : public GameObject {
private:
  // Members
  bool hasConstantSpeed = false;
  float speed = 120.;
  float maxAcceleration = 10.;
  float circleSize;  // todo: is this really needed?

  Vector2f acceleration;
  Vector2f previousAcceleration;  // to draw Acc
  Polygon polygon;

  // Methods
  void resetAcceleration();

public:
  Vector2f velocity;  // todo: make this private
  bool drawAcceleration = false;
  Color32 color;  // todo: make this private

  // Constructor
  explicit Particle(Engine* pEngine, float size = 4.f, Color32 color = Color32::RandomColor(31, 255));

  // Getter / Setters
  Transform getTransform() const { return transform; }

  Vector2f getPosition() const { return transform.position; }

  Vector2f getVelocity() const { return velocity; }

  void setPosition(Vector2f position) { transform.position = position; }

  void setVelocity(Vector2f velocity_) {
    velocity = velocity_;
    transform.rotation = velocity_.normalized();
  }

  void setSpeed(float newSpeed) { speed = newSpeed; }

  void setMaxAcceleration(float newMaxAcceleration) { maxAcceleration = newMaxAcceleration; }

  void setHasConstantSpeed(bool hasConstantSpeed_) { hasConstantSpeed = hasConstantSpeed_; }

  // Methods
  void applyForce(Vector2f force);

  virtual void Update(float deltaTime) override;

  // Inherited via Drawable
  void OnDraw(SDL_Renderer* renderer) override;
};

#endif
