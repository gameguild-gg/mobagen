#ifndef VECTOR3_H
#define VECTOR3_H

//
// just a vector3 simple
struct Vector3 {
    float x;
    float y;
    float z;

    Vector3(){
        x=0;
        y=0;
        z=0;
    }

    Vector3(float x, float y, float z) {
        this->x = x;
        this->y = y;
        this->z = z;
    }
};



#endif
