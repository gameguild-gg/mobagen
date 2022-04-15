#ifndef VECTOR3_H
#define VECTOR3_H

// just a vector3 to store colors
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

    // todo: move this to a Colors context
    // todo: make them static inline constexpr
    static Vector3 Gray(){return{130, 130, 130};}
    static Vector3 LightRed(){return{245, 88, 122};}
    static Vector3 Orange(){return{255, 107, 15};}
    static Vector3 LightOrange(){return{245, 197, 66};}
    static Vector3 LimeGreen(){return{140, 245, 66};}
    static Vector3 LightBlue(){return{66, 188, 245};}
    static Vector3 DarkBlue(){return{91, 15, 255};}
    static Vector3 Purple(){return{138, 66, 245};}
    static Vector3 Pink(){return{245, 88, 232};}

    static Vector3 Black(){return{0, 0, 0};}
    static Vector3 White(){return{255, 255, 255};}
    static Vector3 Red(){return{255, 0, 0};}
    static Vector3 Green(){return{0, 255, 0};}
    static Vector3 Blue(){return{0, 0, 255};}
    static Vector3 Yellow(){return{255, 255, 0};}
    static Vector3 Magenta(){return{255, 0, 255};}
    static Vector3 Cyan(){return{0, 255, 255};}

    static Vector3 RandomColor();
    static Vector3 RandomColor(float min, float max);
};



#endif
