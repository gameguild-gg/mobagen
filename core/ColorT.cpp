#include "ColorT.h"
#include "Random.h"
#include <stdexcept>
#include <algorithm>

Color32::Color32(uint32_t packed) {
    this->a = packed >> 24;
    this->b = (packed << 8) >> 24;
    this->g = (packed << 16) >> 24;
    this->r = (packed << 24) >> 24;
}

Color32::Color32(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    this->a = a;
    this->r = r;
    this->g = g;
    this->b = b;
}

Color32 &Color32::operator=(const Colorf &rhs) {
    a = (uint8_t)(rhs.a*255);
    r = (uint8_t)(rhs.r*255);
    g = (uint8_t)(rhs.g*255);
    b = (uint8_t)(rhs.b*255);
    return *this;
}

bool Color32::operator==(const Color32 &rhs) const {
    return a == rhs.a &&
           r == rhs.r &&
           g == rhs.g &&
           b == rhs.b;
}

bool Color32::operator!=(const Color32 &rhs) const {
    return !(rhs == *this);
}

uint8_t &Color32::operator[](const int &i) {
    if (i < 0 || i > 4) throw std::out_of_range("Out of color range\n");
    switch (i) {
        default:
        case 0:
            return a;
        case 1:
            return r;
        case 2:
            return g;
        case 3:
            return b;
    }
}

const uint8_t &Color32::operator[](const int &i) const {
    if (i < 0 || i > 4) throw std::out_of_range("Out of color range\n");
    switch (i) {
        default:
        case 0:
            return a;
        case 1:
            return r;
        case 2:
            return g;
        case 3:
            return b;
    }
}

Color32 Color32::RandomColor(int min, int max) {
    return Color32(
            Random::Range(min,max),
            Random::Range(min,max),
            Random::Range(min,max),
            255);
}

Color32 Color32::RandomColor(int rmin, int rmax, int gmin, int gmax, int bmin, int bmax, int amin, int amax) {
    return Color32(0);
}

Color32::Color32() {r=0;g=0;b=0,a=255;}

Colorf::Colorf(uint32_t packed) {
    this->a = ((float)(packed >> 24))/255;
    this->r = ((float)((packed << 8) >> 24))/255;
    this->g = ((float)((packed << 16) >> 24))/255;
    this->b = ((float)((packed << 16) >> 24))/255;
}

Colorf &Colorf::operator=(const Color32 &rhs) {
    a = rhs.a/255.f;
    r = rhs.r/255.f;
    g = rhs.g/255.f;
    b = rhs.b/255.f;
    return *this;
}

Colorf::Colorf(float r, float g, float b, float a = 1) {
    this->a=a;
    this->r=r;
    this->g=g;
    this->b=b;
}

void Colorf::RGBtoHSV(Colorf color, float* H, float* S, float* V) {

}

Colorf Colorf::HSVtoRGB(float H, float S, float V, bool hdr) {
    auto white = Colorf(0,0,0);
    if ((double) S == 0.0) {
        white.r = V;
        white.g = V;
        white.b = V;
    }
    else if ((double) V == 0.0)
    {
        white.r = 0.0f;
        white.g = 0.0f;
        white.b = 0.0f;
    }
    else
    {
        white.r = 0.0f;
        white.g = 0.0f;
        white.b = 0.0f;
        float num1 = S;
        float num2 = V;
        float f = H * 6.f;
        int num3 = (int) std::floor(f);
        float num4 = f - (float) num3;
        float num5 = num2 * (1.f - num1);
        float num6 = num2 * (float) (1.0 - (double) num1 * (double) num4);
        float num7 = num2 * (float) (1.0 - (double) num1 * (1.0 - (double) num4));
        switch (num3) {
            case -1:
                white.r = num2;
                white.g = num5;
                white.b = num6;
                break;
            case 0:
                white.r = num2;
                white.g = num7;
                white.b = num5;
                break;
            case 1:
                white.r = num6;
                white.g = num2;
                white.b = num5;
                break;
            case 2:
                white.r = num5;
                white.g = num2;
                white.b = num7;
                break;
            case 3:
                white.r = num5;
                white.g = num6;
                white.b = num2;
                break;
            case 4:
                white.r = num7;
                white.g = num5;
                white.b = num2;
                break;
            case 5:
                white.r = num2;
                white.g = num5;
                white.b = num6;
                break;
            case 6:
                white.r = num2;
                white.g = num7;
                white.b = num5;
                break;
            default:
                throw;
        }
        if (!hdr)
        {
            white.r = std::clamp(white.r, 0.0f, 1.f);
            white.g = std::clamp(white.g, 0.0f, 1.f);
            white.b = std::clamp(white.b, 0.0f, 1.f);
        }
    }
    return white;
}

void Colorf::RGBToHSVHelper(float offset, float dominantcolor, float colorone, float colortwo, float *H, float *S,
                            float *V) {
    *V = dominantcolor;
    if ((double) *V != 0.0)
    {
        float num1 = (double) colorone <= (double) colortwo ? colorone : colortwo;
        float num2 = *V - num1;
        if ((double) num2 != 0.0) {
            *S = num2 / (*V);
            *H = offset + (colorone - colortwo) / num2;
        }
        else {
            *S = 0.0f;
            *H = offset + (colorone - colortwo);
        }
        *H /= 6.f;
        if ((double) *H >= 0.0)
            return;
        ++(*H);
    }
    else
    {
        *S = 0.0f;
        *H = 0.0f;
    }
}

