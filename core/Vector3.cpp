#include "Vector3.h"
#include "Random.h"

Vector3 Vector3::RandomColor() {
    return {
        Random::Range(0,255),
        Random::Range(0,255),
        Random::Range(0,255)};
}

Vector3 Vector3::RandomColor(float min, float max) {
    return {
            Random::Range(min,max),
            Random::Range(min,max),
            Random::Range(min,max)};
}
