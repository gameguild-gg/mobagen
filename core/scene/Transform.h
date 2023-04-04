#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "../math/Vector2.h"

struct Transform {
public:
  Vector2f rotation;  // this is the vector pointing to the up
  Vector2f scale;
  Vector2f position;

  Transform();
  Transform(Vector2f position, Vector2f scale, Vector2f rotation);
};

#endif
