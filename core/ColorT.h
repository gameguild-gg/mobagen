#ifndef COLORT_H
#define COLORT_H

#include <cstdint>
#include "Vector3.h"

/*
 * Color32
struct in UnityEngine/Implemented in:UnityEngine.CoreModuleLeave feedback
Description
Representation of RGBA colors in 32 bit format.

Each color component is a byte value with a range from 0 to 255.

Components (r,g,b) define a color in RGB color space. Alpha component (a) defines transparency - alpha of 255 is completely opaque, alpha of zero is completely transparent.

Properties
a	Alpha component of the color.
b	Blue component of the color.
g	Green component of the color.
r	Red component of the color.
this[int]	Access the red (r), green (g), blue (b), and alpha (a) color components using [0], [1], [2], [3] respectively.
Constructors
Color32	Constructs a new Color32 with given r, g, b, a components.
Public Methods
ToString	Returns a formatted string for this color.
Static Methods
Lerp	Linearly interpolates between colors a and b by t.
LerpUnclamped	Linearly interpolates between colors a and b by t.
Operators
Color	Color32 can be implicitly converted to and from Color.
Color32	Color32 can be implicitly converted to and from Color.
 */

/*
 * Description
Representation of RGBA colors.

This structure is used throughout Unity to pass colors around. Each color component is a floating point value with a range from 0 to 1.

Components (r,g,b) define a color in RGB color space. Alpha component (a) defines transparency - alpha of one is completely opaque, alpha of zero is completely transparent.

Static Properties
black	Solid black. RGBA is (0, 0, 0, 1).
blue	Solid blue. RGBA is (0, 0, 1, 1).
clear	Completely transparent. RGBA is (0, 0, 0, 0).
cyan	Cyan. RGBA is (0, 1, 1, 1).
gray	Gray. RGBA is (0.5, 0.5, 0.5, 1).
green	Solid green. RGBA is (0, 1, 0, 1).
grey	English spelling for gray. RGBA is the same (0.5, 0.5, 0.5, 1).
magenta	Magenta. RGBA is (1, 0, 1, 1).
red	Solid red. RGBA is (1, 0, 0, 1).
white	Solid white. RGBA is (1, 1, 1, 1).
yellow	Yellow. RGBA is (1, 0.92, 0.016, 1), but the color is nice to look at!
Properties
a	Alpha component of the color (0 is transparent, 1 is opaque).
b	Blue component of the color.
g	Green component of the color.
gamma	A version of the color that has had the gamma curve applied.
grayscale	The grayscale value of the color. (Read Only)
linear	A linear value of an sRGB color.
maxColorComponent	Returns the maximum color component value: Max(r,g,b).
r	Red component of the color.
this[int]	Access the r, g, b,a components using [0], [1], [2], [3] respectively.
Constructors
Color	Constructs a new Color with given r,g,b,a components.
Public Methods
ToString	Returns a formatted string of this color.
Static Methods
HSVToRGB	Creates an RGB colour from HSV input.
Lerp	Linearly interpolates between colors a and b by t.
LerpUnclamped	Linearly interpolates between colors a and b by t.
RGBToHSV	Calculates the hue, saturation and value of an RGB input color.
Operators
Color	Colors can be implicitly converted to and from Vector4.
operator -	Subtracts color b from color a. Each component is subtracted separately.
operator *	Multiplies two colors together. Each component is multiplied separately.
operator /	Divides color a by the float b. Each color component is scaled separately.
operator +	Adds two colors together. Each component is added separately.
Vector4	Colors can be implicitly converted to and from Vector4.
 */

struct Colorf;

struct Color32 {
    uint8_t a,r,g,b;
    explicit Color32();
    explicit Color32(uint32_t packed);
    explicit Color32(uint8_t r, uint8_t g, uint8_t b, uint8_t a=255);

    // unary operations
    Color32 operator- () const;
    Color32 operator+ () const;

    // binary operatons
    Color32 operator- (const Color32& rhs) const;
    Color32 operator- (const uint8_t& rhs) const;
    Color32 operator+ (const Color32& rhs) const;
    Color32 operator+ (const uint8_t& rhs) const;
    Color32 operator* (const uint8_t& rhs) const;
    Color32 operator* (const float & rhs) const;
    friend Color32 operator* (const float& lhs, const Color32& rhs);
    friend Color32 operator* (const uint8_t& lhs, const Color32& rhs);
    Color32 operator/ (const float& rhs) const;
    bool operator==(const Color32 &rhs) const;
    bool operator!=(const Color32 &rhs) const;

    // assignment operator
    Color32& operator= (const Colorf& rhs);

    //compound assignment operations
    Color32& operator+= (const Color32& rhs);
    Color32& operator-= (const Color32& rhs);
    Color32& operator*= (const float& rhs);
    Color32& operator/= (const float& rhs);

    // subscript operator
    uint8_t& operator[] (const int& i);
    const uint8_t & operator[] (const int& i) const;

    // random
    static Color32 RandomColor(int min=0, int max=0);
    static Color32 RandomColor(int rmin, int rmax, int gmin, int gmax, int bmin, int bmax, int amin=0, int amax=0);

    // intensity
    Color32 Shade(float);
    Color32 Shade100();
    Color32 Shade200();
    Color32 Shade300();
    Color32 Shade400();
    Color32 Shade600();
    Color32 Shade700();
    Color32 Shade800();
    Color32 Shade900();
    inline Color32 Light() const{return Color32(((short)r+255)/2, ((short)g+255)/2, ((short)b+255)/2);
    };
    inline Color32 Dark() const{return Color32(r/2, g/2, b/2);};
};

struct Colorf {
    float a,r,g,b;
public:
    explicit Colorf(uint32_t packed);
    explicit Colorf(float r, float g, float b, float a);

    // operators
    // todo: create all operators
    Colorf& operator= (const Color32& rhs);

    static void RGBtoHSV(Colorf color, float* H, float* S, float* V);
    static Colorf HSVtoRGB(float H, float S, float V, bool hdr=true);

private:
    static void RGBToHSVHelper(
            float offset,
            float dominantcolor,
            float colorone,
            float colortwo,
            float* H,
            float* S,
            float* V);
};

// todo: create color hsv class

namespace Color {
    static inline const Color32 TransparentBlack = Color32(0);
    static inline const Color32 Transparent = Color32(0);
    static inline const Color32 AliceBlue = Color32(0xfffff8f0);
    static inline const Color32 AntiqueWhite = Color32(0xffd7ebfa);
    static inline const Color32 Aqua = Color32(0xffffff00);
    static inline const Color32 Aquamarine = Color32(0xffd4ff7f);
    static inline const Color32 Azure = Color32(0xfffffff0);
    static inline const Color32 Beige = Color32(0xffdcf5f5);
    static inline const Color32 Bisque = Color32(0xffc4e4ff);
    static inline const Color32 Black = Color32(0xff000000);
    static inline const Color32 BlanchedAlmond = Color32(0xffcdebff);
    static inline const Color32 Blue = Color32(0xffff0000);
    static inline const Color32 BlueViolet = Color32(0xffe22b8a);
    static inline const Color32 Brown = Color32(0xff2a2aa5);
    static inline const Color32 BurlyWood = Color32(0xff87b8de);
    static inline const Color32 CadetBlue = Color32(0xffa09e5f);
    static inline const Color32 Chartreuse = Color32(0xff00ff7f);
    static inline const Color32 Chocolate = Color32(0xff1e69d2);
    static inline const Color32 Coral = Color32(0xff507fff);
    static inline const Color32 CornflowerBlue = Color32(0xffed9564);
    static inline const Color32 Cornsilk = Color32(0xffdcf8ff);
    static inline const Color32 Crimson = Color32(0xff3c14dc);
    static inline const Color32 Cyan = Color32(0xffffff00);
    static inline const Color32 DarkBlue = Color32(0xff8b0000);
    static inline const Color32 DarkCyan = Color32(0xff8b8b00);
    static inline const Color32 DarkGoldenrod = Color32(0xff0b86b8);
    static inline const Color32 DarkGray = Color32(0xffa9a9a9);
    static inline const Color32 DarkGreen = Color32(0xff006400);
    static inline const Color32 DarkKhaki = Color32(0xff6bb7bd);
    static inline const Color32 DarkMagenta = Color32(0xff8b008b);
    static inline const Color32 DarkOliveGreen = Color32(0xff2f6b55);
    static inline const Color32 DarkOrange = Color32(0xff008cff);
    static inline const Color32 DarkOrchid = Color32(0xffcc3299);
    static inline const Color32 DarkRed = Color32(0xff00008b);
    static inline const Color32 DarkSalmon = Color32(0xff7a96e9);
    static inline const Color32 DarkSeaGreen = Color32(0xff8bbc8f);
    static inline const Color32 DarkSlateBlue = Color32(0xff8b3d48);
    static inline const Color32 DarkSlateGray = Color32(0xff4f4f2f);
    static inline const Color32 DarkTurquoise = Color32(0xffd1ce00);
    static inline const Color32 DarkViolet = Color32(0xffd30094);
    static inline const Color32 DeepPink = Color32(0xff9314ff);
    static inline const Color32 DeepSkyBlue = Color32(0xffffbf00);
    static inline const Color32 DimGray = Color32(0xff696969);
    static inline const Color32 DodgerBlue = Color32(0xffff901e);
    static inline const Color32 Firebrick = Color32(0xff2222b2);
    static inline const Color32 FloralWhite = Color32(0xfff0faff);
    static inline const Color32 ForestGreen = Color32(0xff228b22);
    static inline const Color32 Fuchsia = Color32(0xffff00ff);
    static inline const Color32 Gainsboro = Color32(0xffdcdcdc);
    static inline const Color32 GhostWhite = Color32(0xfffff8f8);
    static inline const Color32 Gold = Color32(0xff00d7ff);
    static inline const Color32 Goldenrod = Color32(0xff20a5da);
    static inline const Color32 Gray = Color32(0xff808080);
    static inline const Color32 Green = Color32(0xff008000);
    static inline const Color32 GreenYellow = Color32(0xff2fffad);
    static inline const Color32 Honeydew = Color32(0xfff0fff0);
    static inline const Color32 HotPink = Color32(0xffb469ff);
    static inline const Color32 IndianRed = Color32(0xff5c5ccd);
    static inline const Color32 Indigo = Color32(0xff82004b);
    static inline const Color32 Ivory = Color32(0xfff0ffff);
    static inline const Color32 Khaki = Color32(0xff8ce6f0);
    static inline const Color32 Lavender = Color32(0xfffae6e6);
    static inline const Color32 LavenderBlush = Color32(0xfff5f0ff);
    static inline const Color32 LawnGreen = Color32(0xff00fc7c);
    static inline const Color32 LemonChiffon = Color32(0xffcdfaff);
    static inline const Color32 LightBlue = Color32(0xffe6d8ad);
    static inline const Color32 LightCoral = Color32(0xff8080f0);
    static inline const Color32 LightCyan = Color32(0xffffffe0);
    static inline const Color32 LightGoldenrodYellow = Color32(0xffd2fafa);
    static inline const Color32 LightGray = Color32(0xffd3d3d3);
    static inline const Color32 LightGreen = Color32(0xff90ee90);
    static inline const Color32 LightPink = Color32(0xffc1b6ff);
    static inline const Color32 LightSalmon = Color32(0xff7aa0ff);
    static inline const Color32 LightSeaGreen = Color32(0xffaab220);
    static inline const Color32 LightSkyBlue = Color32(0xffface87);
    static inline const Color32 LightSlateGray = Color32(0xff998877);
    static inline const Color32 LightSteelBlue = Color32(0xffdec4b0);
    static inline const Color32 LightYellow = Color32(0xffe0ffff);
    static inline const Color32 Lime = Color32(0xff00ff00);
    static inline const Color32 LimeGreen = Color32(0xff32cd32);
    static inline const Color32 Linen = Color32(0xffe6f0fa);
    static inline const Color32 Magenta = Color32(0xffff00ff);
    static inline const Color32 Maroon = Color32(0xff000080);
    static inline const Color32 MediumAquamarine = Color32(0xffaacd66);
    static inline const Color32 MediumBlue = Color32(0xffcd0000);
    static inline const Color32 MediumOrchid = Color32(0xffd355ba);
    static inline const Color32 MediumPurple = Color32(0xffdb7093);
    static inline const Color32 MediumSeaGreen = Color32(0xff71b33c);
    static inline const Color32 MediumSlateBlue = Color32(0xffee687b);
    static inline const Color32 MediumSpringGreen = Color32(0xff9afa00);
    static inline const Color32 MediumTurquoise = Color32(0xffccd148);
    static inline const Color32 MediumVioletRed = Color32(0xff8515c7);
    static inline const Color32 MidnightBlue = Color32(0xff701919);
    static inline const Color32 MintCream = Color32(0xfffafff5);
    static inline const Color32 MistyRose = Color32(0xffe1e4ff);
    static inline const Color32 Moccasin = Color32(0xffb5e4ff);
    // static inline const Color32 MonoGameOrange = Color32(0xff003ce7);
    static inline const Color32 NavajoWhite = Color32(0xffaddeff);
    static inline const Color32 Navy = Color32(0xff800000);
    static inline const Color32 OldLace = Color32(0xffe6f5fd);
    static inline const Color32 Olive = Color32(0xff008080);
    static inline const Color32 OliveDrab = Color32(0xff238e6b);
    static inline const Color32 Orange = Color32(0xff00a5ff);
    static inline const Color32 OrangeRed = Color32(0xff0045ff);
    static inline const Color32 Orchid = Color32(0xffd670da);
    static inline const Color32 PaleGoldenrod = Color32(0xffaae8ee);
    static inline const Color32 PaleGreen = Color32(0xff98fb98);
    static inline const Color32 PaleTurquoise = Color32(0xffeeeeaf);
    static inline const Color32 PaleVioletRed = Color32(0xff9370db);
    static inline const Color32 PapayaWhip = Color32(0xffd5efff);
    static inline const Color32 PeachPuff = Color32(0xffb9daff);
    static inline const Color32 Peru = Color32(0xff3f85cd);
    static inline const Color32 Pink = Color32(0xffcbc0ff);
    static inline const Color32 Plum = Color32(0xffdda0dd);
    static inline const Color32 PowderBlue = Color32(0xffe6e0b0);
    static inline const Color32 Purple = Color32(0xff800080);
    static inline const Color32 Red = Color32(0xff0000ff);
    static inline const Color32 RosyBrown = Color32(0xff8f8fbc);
    static inline const Color32 RoyalBlue = Color32(0xffe16941);
    static inline const Color32 SaddleBrown = Color32(0xff13458b);
    static inline const Color32 Salmon = Color32(0xff7280fa);
    static inline const Color32 SandyBrown = Color32(0xff60a4f4);
    static inline const Color32 SeaGreen = Color32(0xff578b2e);
    static inline const Color32 SeaShell = Color32(0xffeef5ff);
    static inline const Color32 Sienna = Color32(0xff2d52a0);
    static inline const Color32 Silver = Color32(0xffc0c0c0);
    static inline const Color32 SkyBlue = Color32(0xffebce87);
    static inline const Color32 SlateBlue = Color32(0xffcd5a6a);
    static inline const Color32 SlateGray = Color32(0xff908070);
    static inline const Color32 Snow = Color32(0xfffafaff);
    static inline const Color32 SpringGreen = Color32(0xff7fff00);
    static inline const Color32 SteelBlue = Color32(0xffb48246);
    static inline const Color32 Tan = Color32(0xff8cb4d2);
    static inline const Color32 Teal = Color32(0xff808000);
    static inline const Color32 Thistle = Color32(0xffd8bfd8);
    static inline const Color32 Tomato = Color32(0xff4763ff);
    static inline const Color32 Turquoise = Color32(0xffd0e040);
    static inline const Color32 Violet = Color32(0xffee82ee);
    static inline const Color32 Wheat = Color32(0xffb3def5);
    static inline const Color32 White = Color32(0xffffffff);
    static inline const Color32 WhiteSmoke = Color32(0xfff5f5f5);
    static inline const Color32 Yellow = Color32(0xff00ffff);
    static inline const Color32 YellowGreen = Color32(0xff32cd9a);
}
#endif //COLORT_H