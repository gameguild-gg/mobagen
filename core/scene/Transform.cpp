#include "Transform.h"

Transform::Transform() {
  this->scale = Vector2f::identity();
  this->position = Vector2f::zero();
  this->rotation = Vector2f::zero();
}

Transform::Transform(Vector2f position, Vector2f scale, Vector2f rotation) {
  this->position = position;
  this->scale = scale;
  this->rotation = rotation;
}
