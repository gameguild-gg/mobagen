#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "Vector2.h"

struct Transform {
    Vector2 rotation; // this is the vector pointing to the up
    Vector2 scale;
    Vector2 position;
};

#endif
