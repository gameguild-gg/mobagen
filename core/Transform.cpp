#include "Transform.h"

Transform::Transform() {
    this->scale = Vector2::identity();
    this->position = Vector2::zero();
    this->rotation = Vector2::zero();
}

Transform::Transform(Vector2 position, Vector2 scale, Vector2 rotation) {
    this->position = position;
    this->scale = scale;
    this->rotation = rotation;
}
