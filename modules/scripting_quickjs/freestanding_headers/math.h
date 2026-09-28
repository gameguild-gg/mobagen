/* Freestanding shim <math.h> — supplied via -isystem for the QuickJS guest. */
#ifndef _SHIM_MATH_H_
#define _SHIM_MATH_H_
double pow(double, double);
double log2(double);
double log(double);
double exp(double);
double fmod(double, double);
double sqrt(double);
double ceil(double);
double floor(double);
double trunc(double);
double scalbn(double, int);
double copysign(double, double);
double frexp(double, int*);
double ldexp(double, int);
double rint(double);
double nearbyint(double);
double round(double);
double sin(double);
double cos(double);
double tan(double);
double atan2(double, double);
double fabs(double);
double hypot(double, double);
double log10(double);
double cbrt(double);
double asin(double);
double acos(double);
double atan(double);
double sinh(double);
double cosh(double);
double tanh(double);
double exp2(double);
double expm1(double);
double log1p(double);
long lrint(double);
double fmin(double, double);
double fmax(double, double);
double acosh(double);
double asinh(double);
double atanh(double);
double log2(double);
int isfinite(double);
int isnan(double);
int isinf(double);
int signbit(double);
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define HUGE_VAL (__builtin_inf())
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define isnan_shim isnan
#endif
